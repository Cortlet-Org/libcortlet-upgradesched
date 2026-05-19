#ifndef CORTLET_UPGRADESCHED_H
#define CORTLET_UPGRADESCHED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Opaque handle for the scheduler instance
    typedef struct cortlet_sched_t cortlet_sched_t;

    // Task callback function pointer layout
    typedef void (*cortlet_task_fn)(void* arg);

    /**
     * Initializes the scheduler, auto-detects CPU topology,
     * spawns worker threads, and pins them 1:1 to physical cores.
     * Returns NULL on failure.
     */
    cortlet_sched_t* cortlet_sched_init(void);

    /**
     * Pushes a task to the queue in O(1) non-blocking time.
     * Uses atomic lock-free scheduling primitives.
     */
    int cortlet_sched_push(cortlet_sched_t* sched, cortlet_task_fn func, void* arg);

    /**
     * Synchronization barrier. Blocks the caller until all currently
     * queued tasks across all queues hit zero.
     */
    void cortlet_sched_wait(cortlet_sched_t* sched);

    /**
     * Gracefully tears down all threads, flushes remaining tasks,
     * and frees internal topology maps.
     */
    void cortlet_sched_destroy(cortlet_sched_t* sched);

#ifdef __cplusplus
}
#endif

#endif // CORTLET_UPGRADESCHED_H
