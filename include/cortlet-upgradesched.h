#ifndef CORTLET_UPGRADESCHED_H
#define CORTLET_UPGRADESCHED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libcortlet-upgradesched
 * Version 2.0.0
 *
 * Public C API for the Rust-powered Cortlet scheduler.
 */

/* ---------------------------------------------------------
 * Version
 * --------------------------------------------------------- */

#define CORTLET_VERSION_MAJOR 2
#define CORTLET_VERSION_MINOR 0
#define CORTLET_VERSION_PATCH 0

uint32_t cortlet_version_major(void);
uint32_t cortlet_version_minor(void);
uint32_t cortlet_version_patch(void);

/* ---------------------------------------------------------
 * Opaque handles
 * --------------------------------------------------------- */

/*
 * The actual scheduler implementation lives in Rust.
 *
 * C programs must never access the internals of this type.
 */
typedef struct CortletScheduler cortlet_sched_t;

/*
 * Opaque task-group handle.
 *
 * The actual task-group implementation lives in Rust.
 */
typedef struct CortletTaskGroup cortlet_task_group_t;

/* ---------------------------------------------------------
 * Task types
 * --------------------------------------------------------- */

/*
 * Function executed by a Cortlet worker.
 *
 * The supplied argument is the same pointer passed during submission.
 */
typedef void (*cortlet_task_fn)(void *argument);

/* ---------------------------------------------------------
 * Result codes
 * --------------------------------------------------------- */

typedef enum cortlet_result {
    /*
     * Operation completed successfully.
     */
    CORTLET_OK = 0,

    /*
     * One or more supplied arguments were invalid.
     */
    CORTLET_INVALID_ARGUMENT = 1,

    /*
     * Memory allocation failed.
     */
    CORTLET_ALLOCATION_FAILED = 2,

    /*
     * The scheduler is shutting down or has already stopped.
     */
    CORTLET_SCHEDULER_STOPPED = 3,

    /*
     * The scheduler could not accept another task because its configured
     * outstanding-task capacity has been reached.
     */
    CORTLET_QUEUE_FULL = 4,

    /*
     * Unexpected internal scheduler failure.
     */
    CORTLET_INTERNAL_ERROR = 255

} cortlet_result_t;

/* ---------------------------------------------------------
 * Task priorities
 * --------------------------------------------------------- */

typedef enum cortlet_priority {
    CORTLET_PRIORITY_LOW = 0,
    CORTLET_PRIORITY_NORMAL = 1,
    CORTLET_PRIORITY_HIGH = 2
} cortlet_priority_t;

/* ---------------------------------------------------------
 * Scheduler configuration
 * --------------------------------------------------------- */

typedef struct cortlet_sched_config {
    /*
     * Number of worker threads.
     *
     * 0 = automatic.
     */
    size_t worker_count;

    /*
     * CPU affinity.
     *
     * 0     = disabled
     * non-0 = enabled
     */
    int pin_workers;

    /*
     * Maximum number of outstanding tasks.
     *
     * This includes queued and currently executing tasks.
     *
     * 0 = unlimited.
     */
    size_t queue_capacity;

} cortlet_sched_config_t;

typedef struct cortlet_sched_stats {
    /*
     * Tasks successfully accepted by the scheduler.
     */
    uint64_t submitted;

    /*
     * Tasks whose callbacks finished execution.
     */
    uint64_t completed;

    /*
     * Submission attempts rejected because bounded capacity was full.
     */
    uint64_t rejected_full;

    /*
     * Attempts to steal work from peer workers.
     */
    uint64_t steals_attempted;

    /*
     * Successful peer-worker steals.
     */
    uint64_t steals_succeeded;
    uint64_t steal_failures;

    uint64_t fairness_yields;

    /*
     * Tasks currently outstanding.
     *
     * Includes queued and currently executing tasks.
     */
    size_t outstanding;

} cortlet_sched_stats_t;

/* ---------------------------------------------------------
 * ABI layout validation
 * --------------------------------------------------------- */

#if defined(__cplusplus)
    #define CORTLET_STATIC_ASSERT(condition, message) \
        static_assert((condition), message)
#else
    #define CORTLET_STATIC_ASSERT(condition, message) \
        _Static_assert((condition), message)
#endif

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, submitted) == 0,
    "ABI mismatch: cortlet_sched_stats_t.submitted"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, completed)
        == sizeof(uint64_t) * 1,
    "ABI mismatch: cortlet_sched_stats_t.completed"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, rejected_full)
        == sizeof(uint64_t) * 2,
    "ABI mismatch: cortlet_sched_stats_t.rejected_full"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, steals_attempted)
        == sizeof(uint64_t) * 3,
    "ABI mismatch: cortlet_sched_stats_t.steals_attempted"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, steals_succeeded)
        == sizeof(uint64_t) * 4,
    "ABI mismatch: cortlet_sched_stats_t.steals_succeeded"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, steal_failures)
        == sizeof(uint64_t) * 5,
    "ABI mismatch: cortlet_sched_stats_t.steal_failures"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, fairness_yields)
        == sizeof(uint64_t) * 6,
    "ABI mismatch: cortlet_sched_stats_t.fairness_yields"
);

CORTLET_STATIC_ASSERT(
    offsetof(cortlet_sched_stats_t, outstanding)
        >= sizeof(uint64_t) * 7,
    "ABI mismatch: cortlet_sched_stats_t.outstanding"
);

/* ---------------------------------------------------------
 * Scheduler lifecycle
 * --------------------------------------------------------- */

/*
 * Creates a scheduler using the default configuration.
 *
 * Defaults:
 *
 *     worker_count = automatic
 *     pin_workers  = enabled
 *
 * Returns:
 *
 *     valid pointer
 *         Scheduler successfully created.
 *
 *     NULL
 *         Scheduler initialization failed.
 */
cortlet_sched_t *
cortlet_sched_init(void);

/*
 * Creates a scheduler using a caller-supplied configuration.
 *
 * Passing NULL returns NULL.
 */
cortlet_sched_t *
cortlet_sched_init_ex(
    const cortlet_sched_config_t *config
);

/*
 * Destroys a scheduler.
 *
 * Existing queued and running work is drained before worker threads exit.
 *
 * Passing NULL is allowed and does nothing.
 *
 * The scheduler pointer becomes invalid after this function returns.
 */
void
cortlet_sched_destroy(
    cortlet_sched_t *scheduler
);

/* ---------------------------------------------------------
 * Scheduler information
 * --------------------------------------------------------- */

/*
 * Returns the number of tasks currently outstanding.
 *
 * Includes queued and executing tasks.
 *
 * Returns 0 when scheduler is NULL.
 */
size_t
cortlet_sched_outstanding_tasks(
    const cortlet_sched_t *scheduler
);

/*
 * Returns the number of workers currently sleeping or preparing
 * to sleep.
 *
 * This value is a live snapshot and may change immediately.
 *
 * Returns 0 when scheduler is NULL.
 */
size_t
cortlet_sched_sleeping_workers(
    const cortlet_sched_t *scheduler
);

/*
 * Returns the maximum number of consecutive HIGH-priority selections
 * before the scheduler enters its fairness path.
 */
size_t
cortlet_sched_high_burst_limit(void);

/*
 * Returns the number of worker threads owned by the scheduler.
 *
 * Returns 0 when scheduler is NULL.
 */
size_t
cortlet_sched_worker_count(
    const cortlet_sched_t *scheduler
);

/*
 * Returns whether worker CPU pinning was requested.
 *
 * Returns:
 *
 *     1 = pinning enabled
 *     0 = pinning disabled
 *
 * Passing NULL returns 0.
 */
int
cortlet_sched_pin_workers(
    const cortlet_sched_t *scheduler
);

/* ---------------------------------------------------------
 * Task submission
 * --------------------------------------------------------- */

/*
 * Submits one NORMAL-priority task for asynchronous execution.
 *
 * scheduler
 *     Scheduler that should execute the task.
 *
 * task
 *     Function to execute.
 *
 * argument
 *     Arbitrary pointer passed to the task.
 *     NULL is allowed.
 *
 * The caller is responsible for ensuring that memory referenced through
 * argument remains valid until the task has completed.
 */
cortlet_result_t
cortlet_sched_push(
    cortlet_sched_t *scheduler,
    cortlet_task_fn task,
    void *argument
);

/*
 * Submits one task using an explicit priority.
 */
cortlet_result_t
cortlet_sched_push_priority(
    cortlet_sched_t *scheduler,
    cortlet_task_fn task,
    void *argument,
    cortlet_priority_t priority
);

/* ---------------------------------------------------------
 * Task groups
 * --------------------------------------------------------- */

/*
 * Creates a new task group.
 *
 * Returns:
 *
 *     valid pointer
 *         Group created successfully.
 *
 *     NULL
 *         Group allocation failed.
 */
cortlet_task_group_t *
cortlet_task_group_create(void);

/*
 * Submits one NORMAL-priority task belonging to a task group.
 */
cortlet_result_t
cortlet_sched_push_group(
    cortlet_sched_t *scheduler,
    cortlet_task_group_t *group,
    cortlet_task_fn task,
    void *argument
);

/*
 * Submits one task belonging to a task group with an explicit priority.
 */
cortlet_result_t
cortlet_sched_push_group_priority(
    cortlet_sched_t *scheduler,
    cortlet_task_group_t *group,
    cortlet_task_fn task,
    void *argument,
    cortlet_priority_t priority
);

/*
 * Returns the configured maximum number of outstanding tasks.
 *
 * Returns:
 *
 *     0
 *         Unlimited capacity, or scheduler is NULL.
 *
 *     non-zero
 *         Maximum number of outstanding tasks.
 */
size_t
cortlet_sched_queue_capacity(
    const cortlet_sched_t *scheduler
);

/*
 * Waits until every outstanding task belonging to this group has
 * completed.
 *
 * This does not wait for unrelated scheduler tasks.
 *
 * Returns:
 *
 *     CORTLET_OK
 *         All group tasks completed.
 *
 *     CORTLET_INVALID_ARGUMENT
 *         group was NULL.
 */
cortlet_result_t
cortlet_task_group_wait(
    cortlet_task_group_t *group
);

/*
 * Destroys the public task-group handle.
 *
 * The group is closed before the handle is destroyed, so new tasks can
 * no longer be attached.
 *
 * Already-submitted tasks remain valid because the scheduler retains
 * internal group references until those tasks complete.
 *
 * Passing NULL is allowed.
 */
void
cortlet_task_group_destroy(
    cortlet_task_group_t *group
);

/* ---------------------------------------------------------
 * Scheduler synchronization
 * --------------------------------------------------------- */

/*
 * Blocks until all outstanding tasks submitted to this scheduler have
 * completed.
 *
 * This includes grouped and non-grouped tasks.
 */
cortlet_result_t
cortlet_sched_wait(
    cortlet_sched_t *scheduler
);

/*
 * Attempts to submit one NORMAL-priority task without waiting for
 * scheduler capacity.
 *
 * Returns CORTLET_QUEUE_FULL immediately when the configured
 * outstanding-task limit has been reached.
 */
cortlet_result_t
cortlet_sched_try_push(
    cortlet_sched_t *scheduler,
    cortlet_task_fn task,
    void *argument
);

/*
 * Attempts to submit one task with an explicit priority without waiting
 * for scheduler capacity.
 *
 * Returns CORTLET_QUEUE_FULL immediately when capacity has been reached.
 */
cortlet_result_t
cortlet_sched_try_push_priority(
    cortlet_sched_t *scheduler,
    cortlet_task_fn task,
    void *argument,
    cortlet_priority_t priority
);

/*
 * Attempts to submit one NORMAL-priority grouped task without waiting
 * for capacity.
 *
 * Returns CORTLET_QUEUE_FULL immediately when capacity is full.
 */
cortlet_result_t
cortlet_sched_try_push_group(
    cortlet_sched_t *scheduler,
    cortlet_task_group_t *group,
    cortlet_task_fn task,
    void *argument
);

/*
 * Attempts to submit one grouped task with explicit priority without
 * waiting for capacity.
 */
cortlet_result_t
cortlet_sched_try_push_group_priority(
    cortlet_sched_t *scheduler,
    cortlet_task_group_t *group,
    cortlet_task_fn task,
    void *argument,
    cortlet_priority_t priority
);

/*
 * Reads a snapshot of scheduler statistics.
 *
 * The scheduler may continue changing immediately after this function
 * returns.
 *
 * Returns CORTLET_INVALID_ARGUMENT if scheduler or stats is NULL.
 */
cortlet_result_t
cortlet_sched_get_stats(
    const cortlet_sched_t *scheduler,
    cortlet_sched_stats_t *stats
);

/*
 * Begins graceful scheduler shutdown.
 *
 * New submissions are rejected after shutdown begins.
 *
 * Already accepted tasks continue running until drained.
 *
 * The scheduler object remains valid until cortlet_sched_destroy()
 * is called.
 */
cortlet_result_t
cortlet_sched_shutdown(
    cortlet_sched_t *scheduler
);

/*
 * Returns whether scheduler shutdown has begun.
 *
 * Returns:
 *
 *     0 = scheduler still accepting work
 *     1 = scheduler stopped / shutdown requested
 *
 * Passing NULL returns 1.
 */
int
cortlet_sched_is_stopped(
    const cortlet_sched_t *scheduler
);

/* ---------------------------------------------------------
 * C++ compatibility
 * --------------------------------------------------------- */

#undef __cplusplus

#endif