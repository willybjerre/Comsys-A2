// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <pthread.h>
// err.h contains various nonstandard BSD extensions, but they are
// very handy.
#include <err.h>


#include "job_queue.h"
#include "histogram.h"

// Globalt histogram + lås (deles af alle workers)
static int g_hist[8] = {0};
static pthread_mutex_t g_hist_mtx = PTHREAD_MUTEX_INITIALIZER;

// Worker: pop filsti -> tæller bits lokalt -> merge/print under lås
static void *worker_main(void *arg) {
  struct job_queue *q = arg;

  for (;;) {
    char *path = NULL;
    if (job_queue_pop(q, (void**)&path) < 0) break;

    FILE *f = fopen(path, "rb");
    if (!f) { warn("failed to open %s", path); free(path); continue; }

    int local[8] = {0};
    int i = 0;
    unsigned char c;

    while (fread(&c, 1, 1, f) == 1) {
      i++;
      update_histogram(local, c);

      // Hver 100k bytes: merge + print, nulstil local
      if ((i % 100000) == 0) {
        pthread_mutex_lock(&g_hist_mtx);
        merge_histogram(local, g_hist);
        print_histogram(g_hist);
        pthread_mutex_unlock(&g_hist_mtx);
        memset(local, 0, sizeof local);
      }
    }

    // Slut-merge + print for filen
    pthread_mutex_lock(&g_hist_mtx);
    merge_histogram(local, g_hist);
    print_histogram(g_hist);
    pthread_mutex_unlock(&g_hist_mtx);

    fclose(f);
    free(path);
  }
  return NULL;
}

int main(int argc, char * const *argv) {
  if (argc < 2) {
    err(1, "usage: paths...");
  }

  // -n N for antal worker-tråde
  int num_threads = 1;
  char * const *paths = &argv[1];

  if (argc > 3 && strcmp(argv[1], "-n") == 0) {
    // Since atoi() simply returns zero on syntax errors, we cannot
    // distinguish between the user entering a zero, or some
    // non-numeric garbage.  In fact, we cannot even tell whether the
    // given option is suffixed by garbage, i.e. '123foo' returns
    // '123'.  A more robust solution would use strtol(), but its
    // interface is more complicated, so here we are.
    num_threads = atoi(argv[2]);
    if (num_threads < 1) {
      err(1, "invalid thread count: %s", argv[2]);
    }
    paths = &argv[3];
  } else {
    paths = &argv[1];
  }

  // Init kø + start workers
  struct job_queue q;
  if (job_queue_init(&q, 256) != 0) errx(1, "job_queue_init failed");

  pthread_t *ths = calloc(num_threads, sizeof(pthread_t));
  if (!ths) err(1, "calloc threads failed");
  for (int i = 0; i < num_threads; i++) {
    if (pthread_create(&ths[i], NULL, worker_main, &q) != 0) err(1, "pthread_create failed");
  }

  // FTS: push alle filer i køen
  int fts_options = FTS_LOGICAL | FTS_NOCHDIR;
  FTS *ftsp = fts_open(paths, fts_options, NULL);
  if (!ftsp) err(1, "fts_open() failed");

  FTSENT *p;
  while ((p = fts_read(ftsp)) != NULL) {
    switch (p->fts_info) {
      case FTS_D: break;
      case FTS_F: {
        char *copy = strdup(p->fts_path); // FTS buffer kan genbruges
        if (!copy) err(1, "strdup failed");
        if (job_queue_push(&q, copy) != 0) { free(copy); errx(1, "job_queue_push failed"); }
        break;
      }
      default: break;
    }
  }
  fts_close(ftsp);

  // Lukker køen + join workers
  if (job_queue_destroy(&q) != 0) errx(1, "job_queue_destroy failed");
  for (int i = 0; i < num_threads; i++) pthread_join(ths[i], NULL);
  free(ths);

  move_lines(9);

  // Print - udskrift;
  pthread_mutex_lock(&g_hist_mtx);
  print_histogram(g_hist);
  pthread_mutex_unlock(&g_hist_mtx);
  return 0;
}