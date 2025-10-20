#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "job_queue.h"

int job_queue_init(struct job_queue *job_queue, int capacity) {
  struct job *jobs = malloc(sizeof(struct job) * capacity);
  if (jobs == NULL) {
    return -1;
  }
  job_queue->jobs = jobs;
  job_queue->isdestroyed = false;
  job_queue->capacity = capacity;
  job_queue->InUseCapacity = 0;
  return 0;
}

int job_queue_destroy(struct job_queue *job_queue) {
  if (job_queue->InUseCapacity != 0) {
    return -1;
  }
  else {
    for (int i = 0; i < job_queue->capacity; i++) {
      free(job_queue->jobs[i].data);
      free(&job_queue->jobs[i]);
    }
    job_queue->isdestroyed = true;
    for (int i = 0; i < job_queue->capacity; i++) {
      free(job_queue->jobs[i].data);
    }
    free(job_queue);
  }
  return 0;
  
}

int job_queue_push(struct job_queue *job_queue, void *data) {
  if (job_queue->InUseCapacity == job_queue->capacity || job_queue->isdestroyed) {
    return -1;
  }
  else {
    struct job new_job;
    new_job.data = data;
    job_queue->InUseCapacity++;
    job_queue->jobs[job_queue->InUseCapacity] = new_job;
    return 0;
  }
}

int job_queue_pop(struct job_queue *job_queue, void **data) {
  if (job_queue->InUseCapacity == 0) {
    job_queue_destroy(job_queue);
    return -1;
  }
  else {
    *data = job_queue->jobs[0].data;
    for (int i = 0; i < job_queue->InUseCapacity - 1; i++) {
      job_queue->jobs[i] = job_queue->jobs[i + 1];
    }
    job_queue->InUseCapacity--;
    return 0;
  }
}
