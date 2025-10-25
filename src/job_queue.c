#include <stdlib.h>
#include "job_queue.h"

// Opretter en tom jobkø med en given størrelse
int job_queue_init(struct job_queue *q, int capacity) {
  if (capacity <= 0) return -1;

  q->buf = (void**)malloc(sizeof(void*) * capacity);
  if (!q->buf) return -1;

  q->capacity = capacity;
  q->size = 0;
  q->head = 0;
  q->tail = 0;
  q->destroyed = false;

  // Initialiser lås og betingelsesvariable
  if (pthread_mutex_init(&q->m, NULL) != 0) return -1;
  if (pthread_cond_init(&q->can_push, NULL) != 0) return -1;
  if (pthread_cond_init(&q->can_pop, NULL) != 0) return -1;

  return 0;
}

// Lægger et nyt element i køen.
// Venter, hvis køen er fuld, og stopper, hvis den er destrueret.
int job_queue_push(struct job_queue *q, void *data) {
  pthread_mutex_lock(&q->m);

  while (q->size == q->capacity && !q->destroyed) {
    pthread_cond_wait(&q->can_push, &q->m);
  }
  if (q->destroyed) {
    pthread_mutex_unlock(&q->m);
    return -1;
  }

  // Tilføjer data og opdater køposition
  q->buf[q->tail] = data;
  q->tail = (q->tail + 1) % q->capacity;
  q->size++;

  // Giver besked til tråde, der venter på data
  pthread_cond_signal(&q->can_pop);
  pthread_mutex_unlock(&q->m);
  return 0;
}

// Fjerner et element fra køen og returnerer det.
// Venter, hvis køen er tom. Returnerer -1 hvis køen er destrueret.
int job_queue_pop(struct job_queue *q, void **data) {
  pthread_mutex_lock(&q->m);

  while (q->size == 0 && !q->destroyed) {
    pthread_cond_wait(&q->can_pop, &q->m);
  }
  if (q->size == 0 && q->destroyed) {
    pthread_mutex_unlock(&q->m);
    return -1;
  }

  // Fjerner data og opdater køposition
  *data = q->buf[q->head];
  q->head = (q->head + 1) % q->capacity;
  q->size--;

  // Giver besked til tråde, der venter på plads
  pthread_cond_signal(&q->can_push);
  pthread_mutex_unlock(&q->m);
  return 0;
}

// Lukker og rydder op i køen.
// Venter, indtil alle jobs er behandlet.
int job_queue_destroy(struct job_queue *q) {
  pthread_mutex_lock(&q->m);

  // Markerer køen som destrueret og væk alle ventende tråde
  q->destroyed = true;
  pthread_cond_broadcast(&q->can_pop);
  pthread_cond_broadcast(&q->can_push);

  // Venter indtil køen er tom
  while (q->size != 0) {
    pthread_cond_wait(&q->can_push, &q->m);
  }

  pthread_mutex_unlock(&q->m);

  // Frigør ressourcer
  pthread_mutex_destroy(&q->m);
  pthread_cond_destroy(&q->can_pop);
  pthread_cond_destroy(&q->can_push);
  free(q->buf);
  return 0;
}