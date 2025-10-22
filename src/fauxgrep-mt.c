// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <err.h>

#include "job_queue.h"

static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-n threads] needle dir...\n", prog);
  exit(1);
}

// Print a single matching line in a thread-safe way.
static void print_match(const char *path, size_t lineno, const char *line) {
  pthread_mutex_lock(&print_lock);
  // line already includes newline from getline()
  fprintf(stdout, "%s:%zu:%s", path, lineno, line);
  fflush(stdout);
  pthread_mutex_unlock(&print_lock);
}


struct worker_args {
  struct job_queue *q;
  const char *needle;
};

static int fauxgrep_file(const char *needle, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;

  char *line = NULL;
  size_t cap = 0;
  ssize_t nread;
  size_t lineno = 0;

  while ((nread = getline(&line, &cap, f)) != -1) {
    (void)nread; // silence unused warning on some compilers
    lineno++;
    if (strstr(line, needle) != NULL) {
      print_match(path, lineno, line);
    }
  }

  free(line);
  fclose(f);
  return 0;
}

static void *worker_main(void *arg) {
  struct worker_args *wa = (struct worker_args*)arg;

  for (;;) {
    char *path = NULL;
    int r = job_queue_pop(wa->q, (void**)&path);
    if (r == -1) {
      // Queue destroyed, no more work.
      break;
    }
    if (r != 0) {
      // Unexpected error — skip and try again.
      continue;
    }
    if (path) {
      (void)fauxgrep_file(wa->needle, path);
      free(path);
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  int nthreads = 4; // default
  int argi = 1;

  // Parse optional -n <threads>
  if (argi + 1 < argc && strcmp(argv[argi], "-n") == 0) {
    char *end = NULL;
    long k = strtol(argv[argi+1], &end, 10);
    if (end == argv[argi+1] || k <= 0) usage(argv[0]);
    nthreads = (int)k;
    argi += 2;
  }

  if (argi >= argc) usage(argv[0]);
  const char *needle = argv[argi++];
  if (argi >= argc) usage(argv[0]); // need at least one dir

  // Remaining args are directories to traverse
  int npaths = argc - argi;
  char **paths = &argv[argi];

  // FTS expects a NULL-terminated list; we can pass argv-slices directly,
  // but ensure NULL termination explicitly.
  char **fts_argv = calloc((size_t)npaths + 1, sizeof(char*));
  if (!fts_argv) err(1, "calloc");
  for (int i = 0; i < npaths; i++) fts_argv[i] = paths[i];
  fts_argv[npaths] = NULL;

  // Init job queue with a moderate capacity (backpressure friendly)
  struct job_queue q;
  if (job_queue_init(&q, nthreads * 16) != 0) {
    errx(1, "failed to init job queue");
  }

  // Launch workers
  pthread_t *ths = calloc((size_t)nthreads, sizeof(pthread_t));
  if (!ths) err(1, "calloc threads");
  struct worker_args wa = { .q = &q, .needle = needle };
  for (int i = 0; i < nthreads; i++) {
    if (pthread_create(&ths[i], NULL, worker_main, &wa) != 0) {
      err(1, "pthread_create");
    }
  }

  // Traverse and enqueue files
  FTS *ftsp = fts_open(fts_argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
  if (!ftsp) err(1, "fts_open");
  for (FTSENT *p; (p = fts_read(ftsp)) != NULL; ) {
    switch (p->fts_info) {
      case FTS_F: { // regular file
        char *dup = strdup(p->fts_path);
        if (!dup) err(1, "strdup");
        if (job_queue_push(&q, dup) != 0) {
          // If push fails (queue destroyed), free and stop
          free(dup);
        }
        break;
      }
      case FTS_D:
      case FTS_DP:
      case FTS_DNR:
      case FTS_ERR:
      case FTS_NS:
      default:
        break;
    }
  }
  fts_close(ftsp);

  // Signal shutdown, wait for workers
  if (job_queue_destroy(&q) != 0) {
    errx(1, "job_queue_destroy failed");
  }
  for (int i = 0; i < nthreads; i++) {
    pthread_join(ths[i], NULL);
  }

  free(ths);
  free(fts_argv);
  return 0;
}
