#define _GNU_SOURCE
#include "../include/cortlet-upgradesched.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdalign.h>

#define MAX_QUEUE_SIZE 4096

typedef struct {
    cortlet_task_fn func;
    void* arg;
} cortlet_task_t;

typedef struct {
    cortlet_task_t buffer[MAX_QUEUE_SIZE];
    _Atomic size_t head;
    _Atomic size_t tail;
} cortlet_queue_t;

// Define the base structural layout without type tags
typedef struct {
    pthread_t thread_id;
    size_t core_id;
    cortlet_queue_t queue;
    _Atomic int running;
    struct cortlet_sched_t* master;
} cortlet_worker_t;

struct cortlet_sched_t {
    // FIX 1: Align individual array pointer addresses to L1/L2 Cache Lines (64 Bytes)
    // to strictly prevent cross-core False Sharing degradation.
    _Alignas(64) cortlet_worker_t* workers;
    size_t num_cores;
    _Atomic size_t tasks_in_flight;
    _Atomic size_t push_index;

    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static int queue_pop(cortlet_queue_t* q, cortlet_task_t* out_task) {
    size_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (h == t) return 0;

    *out_task = q->buffer[h % MAX_QUEUE_SIZE];
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return 1;
}

static int queue_steal(cortlet_queue_t* q, cortlet_task_t* out_task) {
    size_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (h == t) return 0;

    while (h < t) {
        if (atomic_compare_exchange_strong_explicit(&q->head, &h, h + 1,
                                                    memory_order_acq_rel,
                                                    memory_order_acquire)) {
            *out_task = q->buffer[h % MAX_QUEUE_SIZE];
            return 1;
        }
    }
    return 0;
}

static void* worker_loop(void* arg) {
    cortlet_worker_t* self = (cortlet_worker_t*)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(self->core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    cortlet_task_t task;

    while (atomic_load_explicit(&self->running, memory_order_relaxed)) {
        if (queue_pop(&self->queue, &task)) {
            task.func(task.arg);
            atomic_fetch_sub_explicit(&self->master->tasks_in_flight, 1, memory_order_acq_rel);
            continue;
        }

        int stolen = 0;
        for (size_t i = 0; i < self->master->num_cores; i++) {
            if (i == self->core_id) continue;

            if (queue_steal(&self->master->workers[i].queue, &task)) {
                task.func(task.arg);
                atomic_fetch_sub_explicit(&self->master->tasks_in_flight, 1, memory_order_acq_rel);
                stolen = 1;
                break;
            }
        }

        if (!stolen) {
            pthread_mutex_lock(&self->master->lock);

            if (atomic_load_explicit(&self->running, memory_order_relaxed) &&
                atomic_load_explicit(&self->master->tasks_in_flight, memory_order_relaxed) == 0) {
                pthread_cond_wait(&self->master->cond, &self->master->lock);
            }

            pthread_mutex_unlock(&self->master->lock);
        }
    }
    return NULL;
}

cortlet_sched_t* cortlet_sched_init(void) {
    cortlet_sched_t* sched = malloc(sizeof(cortlet_sched_t));
    if (!sched) return NULL;

    sched->num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    // posix_memalign guarantees the allocated block maps perfectly to 64-byte chunks
    if (posix_memalign((void**)&sched->workers, 64, sched->num_cores * sizeof(cortlet_worker_t)) != 0) {
        free(sched);
        return NULL;
    }

    atomic_init(&sched->tasks_in_flight, 0);
    atomic_init(&sched->push_index, 0);

    pthread_mutex_init(&sched->lock, NULL);
    pthread_cond_init(&sched->cond, NULL);

    for (size_t i = 0; i < sched->num_cores; i++) {
        sched->workers[i].core_id = i;
        sched->workers[i].master = sched;
        atomic_init(&sched->workers[i].running, 1);
        atomic_init(&sched->workers[i].queue.head, 0);
        atomic_init(&sched->workers[i].queue.tail, 0);

        if (pthread_create(&sched->workers[i].thread_id, NULL, worker_loop, &sched->workers[i]) != 0) {
            return NULL;
        }
    }
    return sched;
}

int cortlet_sched_push(cortlet_sched_t* sched, cortlet_task_fn func, void* arg) {
    if (!sched || !func) return -1;

    size_t target_core = atomic_fetch_add_explicit(&sched->push_index, 1, memory_order_relaxed) % sched->num_cores;
    cortlet_queue_t* q = &sched->workers[target_core].queue;

    size_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t h = atomic_load_explicit(&q->head, memory_order_acquire);

    while ((t - h) >= MAX_QUEUE_SIZE) {
        sched_yield();
        h = atomic_load_explicit(&q->head, memory_order_acquire);
        t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    }

    q->buffer[t % MAX_QUEUE_SIZE] = (cortlet_task_t){.func = func, .arg = arg};
    atomic_fetch_add_explicit(&sched->tasks_in_flight, 1, memory_order_relaxed);
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);

    pthread_cond_signal(&sched->cond);

    return 0;
}

void cortlet_sched_wait(cortlet_sched_t* sched) {
    if (!sched) return;
    while (atomic_load_explicit(&sched->tasks_in_flight, memory_order_acquire) > 0) {
        usleep(100);
    }
}

void cortlet_sched_destroy(cortlet_sched_t* sched) {
    if (!sched) return;

    pthread_mutex_lock(&sched->lock);
    for (size_t i = 0; i < sched->num_cores; i++) {
        atomic_store_explicit(&sched->workers[i].running, 0, memory_order_relaxed);
    }
    pthread_cond_broadcast(&sched->cond);
    pthread_mutex_unlock(&sched->lock);

    for (size_t i = 0; i < sched->num_cores; i++) {
        pthread_join(sched->workers[i].thread_id, NULL);
    }

    pthread_mutex_destroy(&sched->lock);
    pthread_cond_destroy(&sched->cond);
    free(sched->workers);
    free(sched);
}
