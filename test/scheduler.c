#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

#include "cortlet-upgradesched.h"

#define BATCH_SIZE 10000UL
#define BATCH_COUNT 10UL
#define PRIORITY_TEST_COUNT 6
#define CONCURRENT_SUBMITTER_THREADS 8U
#define TASKS_PER_SUBMITTER 25000U
#define BOUNDED_CONCURRENT_THREADS 8U
#define BOUNDED_CONCURRENT_ATTEMPTS 1000U

static _Atomic unsigned int concurrent_completed = 0;
static _Atomic int bounded_gate_open = 0;
static _Atomic unsigned int bounded_completed = 0;
static _Atomic int bounded_concurrent_gate_open = 0;
static _Atomic unsigned int bounded_concurrent_completed = 0;
static _Atomic unsigned int bounded_concurrent_accepted = 0;
static _Atomic unsigned int bounded_concurrent_full = 0;
static _Atomic unsigned int push_try_group_completed = 0;
static _Atomic unsigned int stats_completed = 0;
static _Atomic unsigned long completed_tasks = 0;
static _Atomic unsigned long long value_sum = 0;
static _Atomic unsigned int priority_order_index = 0;
static _Atomic unsigned int group_a_count = 0;
static _Atomic unsigned int group_b_count = 0;
static _Atomic unsigned int destroyed_group_count = 0;
static _Atomic unsigned int steal_stats_completed = 0;
static _Atomic unsigned int priority_preempt_index = 0;
static unsigned int priority_preempt_order[128];
static _Atomic unsigned int fairness_high_completed = 0;
static _Atomic unsigned int fairness_low_completed = 0;
static _Atomic unsigned int progress_high_completed = 0;
static _Atomic unsigned int progress_low_completed = 0;
static _Atomic unsigned int high_count_when_first_low_completed = 0;
static _Atomic int first_low_recorded = 0;
static _Atomic unsigned int live_high_completed = 0;
static _Atomic unsigned int live_low_completed = 0;
static _Atomic unsigned int live_high_at_first_low = 0;
static _Atomic int live_first_low_recorded = 0;
static _Atomic unsigned int fairness_stats_high_completed = 0;
static _Atomic unsigned int fairness_stats_low_completed = 0;
static _Atomic int introspection_gate = 0;
static _Atomic unsigned int introspection_completed = 0;

static void introspection_blocking_task(void *argument)
{
    (void)argument;

    while (
        atomic_load_explicit(
            &introspection_gate,
            memory_order_acquire
        )
        == 0
    ) {
    }

    atomic_fetch_add_explicit(
        &introspection_completed,
        1U,
        memory_order_relaxed
    );
}
static void fairness_stats_high_task(void *argument)
{
    (void)argument;

    volatile unsigned int sink = 0;

    for (
        unsigned int i = 0;
        i < 20000U;
        ++i
    ) {
        sink += i;
    }

    (void)sink;

    atomic_fetch_add_explicit(
        &fairness_stats_high_completed,
        1U,
        memory_order_relaxed
    );
}

static void fairness_stats_low_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &fairness_stats_low_completed,
        1U,
        memory_order_relaxed
    );
}
static void live_high_task(void *argument)
{
    (void)argument;

    volatile unsigned int sink = 0;

    for (
        unsigned int i = 0;
        i < 10000U;
        ++i
    ) {
        sink += i;
    }

    (void)sink;

    atomic_fetch_add_explicit(
        &live_high_completed,
        1U,
        memory_order_relaxed
    );
}

static void live_low_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &live_low_completed,
        1U,
        memory_order_relaxed
    );

    int expected = 0;

    if (
        atomic_compare_exchange_strong_explicit(
            &live_first_low_recorded,
            &expected,
            1,
            memory_order_relaxed,
            memory_order_relaxed
        )
    ) {
        unsigned int high_completed =
            atomic_load_explicit(
                &live_high_completed,
                memory_order_relaxed
            );

        atomic_store_explicit(
            &live_high_at_first_low,
            high_completed,
            memory_order_relaxed
        );
    }
}
static void progress_high_task(void *argument)
{
    (void)argument;

    /*
     * Keep HIGH work alive long enough for fairness behavior
     * to become observable.
     */
    volatile unsigned int sink = 0;

    for (
        unsigned int i = 0;
        i < 5000U;
        ++i
    ) {
        sink += i;
    }

    (void)sink;

    atomic_fetch_add_explicit(
        &progress_high_completed,
        1U,
        memory_order_relaxed
    );
}

static void progress_low_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &progress_low_completed,
        1U,
        memory_order_relaxed
    );

    int expected = 0;

    if (
        atomic_compare_exchange_strong_explicit(
            &first_low_recorded,
            &expected,
            1,
            memory_order_relaxed,
            memory_order_relaxed
        )
    ) {
        unsigned int high_completed =
            atomic_load_explicit(
                &progress_high_completed,
                memory_order_relaxed
            );

        atomic_store_explicit(
            &high_count_when_first_low_completed,
            high_completed,
            memory_order_relaxed
        );
    }
}
static void fairness_high_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &fairness_high_completed,
        1U,
        memory_order_relaxed
    );
}

static void fairness_low_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &fairness_low_completed,
        1U,
        memory_order_relaxed
    );
}
static void priority_preempt_task(void *argument)
{
    unsigned int value =
        (unsigned int)(uintptr_t)argument;

    unsigned int index =
        atomic_fetch_add_explicit(
            &priority_preempt_index,
            1U,
            memory_order_relaxed
        );

    if (index < 128U) {
        priority_preempt_order[index] = value;
    }

    /*
     * Keep LOW work around long enough for HIGH work
     * to arrive while the worker is busy.
     */
    if (value < 100U) {
        volatile unsigned int sink = 0;

        for (
            unsigned int i = 0;
            i < 500000U;
            ++i
        ) {
            sink += i;
        }

        (void)sink;
    }
}
static void steal_stats_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &steal_stats_completed,
        1U,
        memory_order_relaxed
    );

    /*
     * Small CPU loop so workers have time to overlap and steal.
     */
    volatile unsigned int sink = 0;

    for (
        unsigned int i = 0;
        i < 1000U;
        ++i
    ) {
        sink += i;
    }

    (void)sink;
}
static void stats_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &stats_completed,
        1U,
        memory_order_relaxed
    );
}
static void push_try_group_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &push_try_group_completed,
        1U,
        memory_order_relaxed
    );
}
typedef struct bounded_concurrent_args {
    cortlet_sched_t *scheduler;
} bounded_concurrent_args_t;

static void bounded_concurrent_task(void *argument)
{
    (void)argument;

    while (
        atomic_load_explicit(
            &bounded_concurrent_gate_open,
            memory_order_acquire
        ) == 0
    ) {
    }

    atomic_fetch_add_explicit(
        &bounded_concurrent_completed,
        1U,
        memory_order_relaxed
    );
}

static void *bounded_concurrent_submitter(void *argument)
{
    bounded_concurrent_args_t *args =
        (bounded_concurrent_args_t *)argument;

    for (
        unsigned int i = 0;
        i < BOUNDED_CONCURRENT_ATTEMPTS;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_try_push(
                args->scheduler,
                bounded_concurrent_task,
                NULL
            );

        if (result == CORTLET_OK) {
            atomic_fetch_add_explicit(
                &bounded_concurrent_accepted,
                1U,
                memory_order_relaxed
            );
        } else if (result == CORTLET_QUEUE_FULL) {
            atomic_fetch_add_explicit(
                &bounded_concurrent_full,
                1U,
                memory_order_relaxed
            );
        } else {
            return (void *)1;
        }
    }

    return NULL;
}
static void bounded_blocking_task(void *argument)
{
    (void)argument;

    while (
        atomic_load_explicit(
            &bounded_gate_open,
            memory_order_acquire
        )
        == 0
    ) {
        /*
         * Busy-wait intentionally for this deterministic scheduler test.
         */
    }

    atomic_fetch_add_explicit(
        &bounded_completed,
        1U,
        memory_order_relaxed
    );
}

static void bounded_normal_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &bounded_completed,
        1U,
        memory_order_relaxed
    );
}
typedef struct concurrent_submitter_args {
    cortlet_sched_t *scheduler;
    unsigned int task_count;
    int failed;
} concurrent_submitter_args_t;

static void concurrent_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &concurrent_completed,
        1U,
        memory_order_relaxed
    );
}

static void *concurrent_submitter_main(void *argument)
{
    concurrent_submitter_args_t *args =
        (concurrent_submitter_args_t *)argument;

    for (
        unsigned int i = 0;
        i < args->task_count;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_push(
                args->scheduler,
                concurrent_task,
                NULL
            );

        if (result != CORTLET_OK) {
            args->failed = 1;

            return NULL;
        }
    }

    return NULL;
}

static void destroyed_group_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &destroyed_group_count,
        1U,
        memory_order_relaxed
    );
}

static void group_a_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &group_a_count,
        1U,
        memory_order_relaxed
    );
}

static void group_b_task(void *argument)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &group_b_count,
        1U,
        memory_order_relaxed
    );
}

static int priority_order[PRIORITY_TEST_COUNT];

typedef struct priority_task_data {
    int marker;
} priority_task_data_t;

static void priority_test_task(void *argument)
{
    priority_task_data_t *data =
        (priority_task_data_t *)argument;

    unsigned int index =
        atomic_fetch_add_explicit(
            &priority_order_index,
            1U,
            memory_order_relaxed
        );

    if (index < PRIORITY_TEST_COUNT) {
        priority_order[index] =
            data->marker;
    }
}

/*
 * ---------------------------------------------------------
 * Test task
 * ---------------------------------------------------------
 */

static void scheduler_task(void *argument)
{
    uintptr_t value =
        (uintptr_t)argument;

    atomic_fetch_add_explicit(
        &completed_tasks,
        1UL,
        memory_order_relaxed
    );

    atomic_fetch_add_explicit(
        &value_sum,
        (unsigned long long)value,
        memory_order_relaxed
    );
}

/*
 * ---------------------------------------------------------
 * Expected sum helper
 * ---------------------------------------------------------
 */

static unsigned long long expected_sum(
    unsigned long start,
    unsigned long count
)
{
    unsigned long long first =
        (unsigned long long)start;

    unsigned long long last =
        (unsigned long long)(start + count - 1UL);

    return
        ((first + last)
        * (unsigned long long)count)
        / 2ULL;
}

/*
 * ---------------------------------------------------------
 * Basic scheduler creation
 * ---------------------------------------------------------
 */

static int test_creation(void)
{
    printf(
        "[TEST] scheduler creation\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: cortlet_sched_init() returned NULL.\n"
        );

        return EXIT_FAILURE;
    }

    size_t workers =
        cortlet_sched_worker_count(
            scheduler
        );

    if (workers == 0) {
        fprintf(
            stderr,
            "FAIL: scheduler reported zero workers.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       workers: %zu\n",
        workers
    );

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: scheduler creation\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Single task
 * ---------------------------------------------------------
 */

static int test_single_task(void)
{
    printf(
        "[TEST] single task\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &completed_tasks,
        0UL,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &value_sum,
        0ULL,
        memory_order_relaxed
    );

    cortlet_result_t result =
        cortlet_sched_push(
            scheduler,
            scheduler_task,
            (void *)(uintptr_t)42
        );

    if (result != CORTLET_OK) {
        fprintf(
            stderr,
            "FAIL: single task submission returned %d.\n",
            (int)result
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    result =
        cortlet_sched_wait(
            scheduler
        );

    if (result != CORTLET_OK) {
        fprintf(
            stderr,
            "FAIL: wait returned %d.\n",
            (int)result
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned long completed =
        atomic_load_explicit(
            &completed_tasks,
            memory_order_relaxed
        );

    unsigned long long sum =
        atomic_load_explicit(
            &value_sum,
            memory_order_relaxed
        );

    if (completed != 1UL) {
        fprintf(
            stderr,
            "FAIL: expected 1 completed task, got %lu.\n",
            completed
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (sum != 42ULL) {
        fprintf(
            stderr,
            "FAIL: expected value sum 42, got %llu.\n",
            sum
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: single task\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Multiple batches
 * ---------------------------------------------------------
 */

static int test_multiple_batches(void)
{
    printf(
        "[TEST] multiple batches\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &completed_tasks,
        0UL,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &value_sum,
        0ULL,
        memory_order_relaxed
    );

    unsigned long total_submitted = 0UL;
    unsigned long long expected_total = 0ULL;

    for (
        unsigned long batch = 0UL;
        batch < BATCH_COUNT;
        ++batch
    ) {
        unsigned long start =
            batch * BATCH_SIZE;

        for (
            unsigned long i = 0UL;
            i < BATCH_SIZE;
            ++i
        ) {
            uintptr_t value =
                (uintptr_t)(start + i);

            cortlet_result_t result =
                cortlet_sched_push(
                    scheduler,
                    scheduler_task,
                    (void *)value
                );

            if (result != CORTLET_OK) {
                fprintf(
                    stderr,
                    "FAIL: submission failed in batch %lu "
                    "at task %lu with result %d.\n",
                    batch,
                    i,
                    (int)result
                );

                cortlet_sched_destroy(
                    scheduler
                );

                return EXIT_FAILURE;
            }
        }

        cortlet_result_t wait_result =
            cortlet_sched_wait(
                scheduler
            );

        if (wait_result != CORTLET_OK) {
            fprintf(
                stderr,
                "FAIL: wait failed after batch %lu "
                "with result %d.\n",
                batch,
                (int)wait_result
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }

        total_submitted +=
            BATCH_SIZE;

        expected_total +=
            expected_sum(
                start,
                BATCH_SIZE
            );

        unsigned long completed =
            atomic_load_explicit(
                &completed_tasks,
                memory_order_relaxed
            );

        printf(
            "       batch %lu/%lu completed "
            "(%lu tasks total)\n",
            batch + 1UL,
            BATCH_COUNT,
            completed
        );

        if (completed != total_submitted) {
            fprintf(
                stderr,
                "FAIL: expected %lu completed tasks, got %lu.\n",
                total_submitted,
                completed
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    unsigned long long sum =
        atomic_load_explicit(
            &value_sum,
            memory_order_relaxed
        );

    if (sum != expected_total) {
        fprintf(
            stderr,
            "FAIL: expected sum %llu, got %llu.\n",
            expected_total,
            sum
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: multiple batches\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Repeated wait
 * ---------------------------------------------------------
 */

static int test_repeated_wait(void)
{
    printf(
        "[TEST] repeated wait\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    /*
     * Waiting on an idle scheduler should succeed immediately.
     */
    for (
        unsigned int i = 0U;
        i < 100U;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_wait(
                scheduler
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "FAIL: idle wait %u returned %d.\n",
                i,
                (int)result
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: repeated wait\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Destroy while work exists
 * ---------------------------------------------------------
 */

static int test_destroy_drains(void)
{
    printf(
        "[TEST] destroy drains queued work\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &completed_tasks,
        0UL,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &value_sum,
        0ULL,
        memory_order_relaxed
    );

    const unsigned long task_count =
        50000UL;

    for (
        unsigned long i = 0UL;
        i < task_count;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_push(
                scheduler,
                scheduler_task,
                (void *)(uintptr_t)i
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "FAIL: destroy-drain submission "
                "failed at %lu.\n",
                i
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * No explicit wait().
     *
     * cortlet_sched_destroy() must drain outstanding work.
     */
    cortlet_sched_destroy(
        scheduler
    );

    unsigned long completed =
        atomic_load_explicit(
            &completed_tasks,
            memory_order_relaxed
        );

    if (completed != task_count) {
        fprintf(
            stderr,
            "FAIL: destroy drained %lu/%lu tasks.\n",
            completed,
            task_count
        );

        return EXIT_FAILURE;
    }

    printf(
        "       drained: %lu/%lu\n",
        completed,
        task_count
    );

    printf(
        "PASS: destroy drains queued work\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Invalid API arguments
 * ---------------------------------------------------------
 */

static int test_invalid_arguments(void)
{
    printf(
        "[TEST] invalid arguments\n"
    );

    if (
        cortlet_sched_wait(NULL)
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: NULL wait did not return "
            "CORTLET_INVALID_ARGUMENT.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push(
            NULL,
            scheduler_task,
            NULL
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: NULL scheduler push did not return "
            "CORTLET_INVALID_ARGUMENT.\n"
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push(
            scheduler,
            NULL,
            NULL
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: NULL task did not return "
            "CORTLET_INVALID_ARGUMENT.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
    cortlet_task_group_wait(NULL)
    != CORTLET_INVALID_ARGUMENT
) {
        fprintf(
            stderr,
            "FAIL: task_group_wait(NULL) did not return "
            "CORTLET_INVALID_ARGUMENT.\n"
        );

        return EXIT_FAILURE;
}

    cortlet_sched_destroy(
        scheduler
    );

    /*
 * Invalid raw priority values must be rejected.
 */
    if (
        cortlet_sched_push_priority(
            scheduler,
            concurrent_task,
            NULL,
            (cortlet_priority_t)99
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: invalid priority was accepted.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_t *invalid_priority_group =
        cortlet_task_group_create();

    if (invalid_priority_group == NULL) {
        fprintf(
            stderr,
            "FAIL: task group creation failed during invalid priority test.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_group_priority(
            scheduler,
            invalid_priority_group,
            concurrent_task,
            NULL,
            (cortlet_priority_t)99
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: invalid grouped priority was accepted.\n"
        );

        cortlet_task_group_destroy(
            invalid_priority_group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_destroy(
        invalid_priority_group
    );

    printf(
        "PASS: invalid arguments\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_custom_configuration(void)
{
    printf(
        "[TEST] custom configuration\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: custom scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    size_t workers =
        cortlet_sched_worker_count(
            scheduler
        );

    if (workers != 4) {
        fprintf(
            stderr,
            "FAIL: expected 4 workers, got %zu.\n",
            workers
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    int pinning =
        cortlet_sched_pin_workers(
            scheduler
        );

    if (pinning != 0) {
        fprintf(
            stderr,
            "FAIL: expected pinning disabled, got %d.\n",
            pinning
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: custom configuration\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_auto_worker_configuration(void)
{
    printf(
        "[TEST] automatic worker configuration\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 0,
        .pin_workers = 0,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: automatic scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    size_t workers =
        cortlet_sched_worker_count(
            scheduler
        );

    if (workers == 0) {
        fprintf(
            stderr,
            "FAIL: automatic worker count resolved to zero.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       automatic workers: %zu\n",
        workers
    );

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: automatic worker configuration\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_pinning_configuration(void)
{
    printf(
        "[TEST] CPU pinning configuration\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 2,
        .pin_workers = 1,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: pinned scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_worker_count(
            scheduler
        )
        != 2
    ) {
        fprintf(
            stderr,
            "FAIL: pinned scheduler worker count mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_pin_workers(
            scheduler
        )
        != 1
    ) {
        fprintf(
            stderr,
            "FAIL: pinning configuration not preserved.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: CPU pinning configuration\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_null_configuration(void)
{
    printf(
        "[TEST] NULL configuration\n"
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            NULL
        );

    if (scheduler != NULL) {
        fprintf(
            stderr,
            "FAIL: init_ex(NULL) should return NULL.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "PASS: NULL configuration\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_task_priorities(void)
{
    printf(
        "[TEST] task priorities\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 1,
        .pin_workers = 0,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: priority scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &priority_order_index,
        0U,
        memory_order_relaxed
    );

    for (
        unsigned int i = 0;
        i < PRIORITY_TEST_COUNT;
        ++i
    ) {
        priority_order[i] = -1;
    }

    priority_task_data_t low1 = {
        .marker = 10
    };

    priority_task_data_t low2 = {
        .marker = 11
    };

    priority_task_data_t normal1 = {
        .marker = 20
    };

    priority_task_data_t normal2 = {
        .marker = 21
    };

    priority_task_data_t high1 = {
        .marker = 30
    };

    priority_task_data_t high2 = {
        .marker = 31
    };

    /*
     * Submit lower-priority work first.
     *
     * A single worker makes the priority selection deterministic.
     */
    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &low1,
            CORTLET_PRIORITY_LOW
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: low-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &low2,
            CORTLET_PRIORITY_LOW
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: low-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &normal1,
            CORTLET_PRIORITY_NORMAL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: normal-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &normal2,
            CORTLET_PRIORITY_NORMAL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: normal-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &high1,
            CORTLET_PRIORITY_HIGH
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: high-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_test_task,
            &high2,
            CORTLET_PRIORITY_HIGH
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: high-priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: priority wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       execution order: "
        "%d %d %d %d %d %d\n",
        priority_order[0],
        priority_order[1],
        priority_order[2],
        priority_order[3],
        priority_order[4],
        priority_order[5]
    );

    /*
     * We expect the HIGH lane to be selected before NORMAL and LOW.
     *
     * Because the worker may begin executing immediately after the first
     * submission, the very first task is allowed to already be running.
     *
     * What matters is that once all lanes contain work, higher-priority
     * tasks are preferred.
     */
    int high_seen_before_low = 0;

    for (
        unsigned int i = 0;
        i < PRIORITY_TEST_COUNT;
        ++i
    ) {
        if (
            priority_order[i] == 30
            || priority_order[i] == 31
        ) {
            high_seen_before_low = 1;
            break;
        }

        if (
            priority_order[i] == 10
            || priority_order[i] == 11
        ) {
            continue;
        }
    }

    if (!high_seen_before_low) {
        fprintf(
            stderr,
            "FAIL: high-priority tasks were not observed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: task priorities\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_task_groups(void)
{
    printf(
        "[TEST] task groups\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_t *group_a =
        cortlet_task_group_create();

    cortlet_task_group_t *group_b =
        cortlet_task_group_create();

    if (
        group_a == NULL
        || group_b == NULL
    ) {
        fprintf(
            stderr,
            "FAIL: task group creation failed.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &group_a_count,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &group_b_count,
        0U,
        memory_order_relaxed
    );

    const unsigned int group_a_tasks = 10000U;
    const unsigned int group_b_tasks = 20000U;

    for (
        unsigned int i = 0;
        i < group_a_tasks;
        ++i
    ) {
        if (
            cortlet_sched_push_group(
                scheduler,
                group_a,
                group_a_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: group A submission failed.\n"
            );

            cortlet_task_group_destroy(
                group_a
            );

            cortlet_task_group_destroy(
                group_b
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    for (
        unsigned int i = 0;
        i < group_b_tasks;
        ++i
    ) {
        if (
            cortlet_sched_push_group_priority(
                scheduler,
                group_b,
                group_b_task,
                NULL,
                CORTLET_PRIORITY_HIGH
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: group B submission failed.\n"
            );

            cortlet_task_group_destroy(
                group_a
            );

            cortlet_task_group_destroy(
                group_b
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Wait only for group A.
     */
    if (
        cortlet_task_group_wait(
            group_a
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: group A wait failed.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed_a =
        atomic_load_explicit(
            &group_a_count,
            memory_order_relaxed
        );

    printf(
        "       group A completed: %u/%u\n",
        completed_a,
        group_a_tasks
    );

    if (
        completed_a
        != group_a_tasks
    ) {
        fprintf(
            stderr,
            "FAIL: group A did not complete fully.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Now wait only for group B.
     */
    if (
        cortlet_task_group_wait(
            group_b
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: group B wait failed.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed_b =
        atomic_load_explicit(
            &group_b_count,
            memory_order_relaxed
        );

    printf(
        "       group B completed: %u/%u\n",
        completed_b,
        group_b_tasks
    );

    if (
        completed_b
        != group_b_tasks
    ) {
        fprintf(
            stderr,
            "FAIL: group B did not complete fully.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Global wait should now return immediately.
     */
    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_task_group_destroy(
            group_a
        );

        cortlet_task_group_destroy(
            group_b
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_destroy(
        group_a
    );

    cortlet_task_group_destroy(
        group_b
    );

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: task groups\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_destroy_group_with_pending_tasks(void)
{
    printf(
        "[TEST] destroy group with pending tasks\n"
    );

    const unsigned int task_count = 50000U;

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
.queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_t *group =
        cortlet_task_group_create();

    if (group == NULL) {
        fprintf(
            stderr,
            "FAIL: task group creation failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &destroyed_group_count,
        0U,
        memory_order_relaxed
    );

    for (
        unsigned int i = 0;
        i < task_count;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_push_group(
                scheduler,
                group,
                destroyed_group_task,
                NULL
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "FAIL: grouped task submission failed at task %u.\n",
                i
            );

            cortlet_task_group_destroy(
                group
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Destroy the public group handle while tasks may still be queued
     * or running.
     *
     * Each scheduled task should retain its own internal Arc reference,
     * so destroying this handle must not invalidate those tasks.
     */
    cortlet_task_group_destroy(
        group
    );

    /*
     * The group pointer is now invalid and must not be used again.
     */
    group = NULL;

    /*
     * Wait on the scheduler instead.
     *
     * Every grouped task should still execute successfully.
     */
    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &destroyed_group_count,
            memory_order_relaxed
        );

    printf(
        "       completed after group destroy: %u/%u\n",
        completed,
        task_count
    );

    if (completed != task_count) {
        fprintf(
            stderr,
            "FAIL: tasks were lost after group destruction.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: destroy group with pending tasks\n\n"
    );

    return EXIT_SUCCESS;
}
static int test_bounded_queue_capacity(void)
{
    printf(
        "[TEST] bounded queue capacity\n"
    );

    /*
     * One worker + capacity 4 gives us a deterministic setup:
     *
     * 1 running blocked task
     * 3 additional outstanding tasks
     * -----------------------------
     * 4 total = capacity reached
     */
    cortlet_sched_config_t config = {
        .worker_count = 1,
        .pin_workers = 0,
        .queue_capacity = 4
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: bounded scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_queue_capacity(
            scheduler
        )
        != 4U
    ) {
        fprintf(
            stderr,
            "FAIL: queue capacity getter returned wrong value.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &bounded_gate_open,
        0,
        memory_order_release
    );

    atomic_store_explicit(
        &bounded_completed,
        0U,
        memory_order_relaxed
    );

    /*
     * Occupy the only worker.
     */
    if (
        cortlet_sched_push(
            scheduler,
            bounded_blocking_task,
            NULL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: blocking task submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Fill the remaining three outstanding-task slots.
     */
    for (
        unsigned int i = 0;
        i < 3U;
        ++i
    ) {
        if (
            cortlet_sched_push(
                scheduler,
                bounded_normal_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: capacity-fill submission %u failed.\n",
                i
            );

            atomic_store_explicit(
                &bounded_gate_open,
                1,
                memory_order_release
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Capacity is now exactly full.
     *
     * Normal push should be rejected.
     */
    cortlet_result_t full_result =
        cortlet_sched_push(
            scheduler,
            bounded_normal_task,
            NULL
        );

    if (
        full_result
        != CORTLET_QUEUE_FULL
    ) {
        fprintf(
            stderr,
            "FAIL: expected CORTLET_QUEUE_FULL, got %d.\n",
            (int)full_result
        );

        atomic_store_explicit(
            &bounded_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       capacity reached correctly: 4/4\n"
    );

    /*
     * try_push must also fail immediately while capacity is full.
     */
    cortlet_result_t try_result =
        cortlet_sched_try_push(
            scheduler,
            bounded_normal_task,
            NULL
        );

    if (
        try_result
        != CORTLET_QUEUE_FULL
    ) {
        fprintf(
            stderr,
            "FAIL: try_push did not return CORTLET_QUEUE_FULL.\n"
        );

        atomic_store_explicit(
            &bounded_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       try_push correctly returned QUEUE_FULL\n"
    );

    /*
     * Priority try_push must behave the same way.
     */
    cortlet_result_t try_priority_result =
        cortlet_sched_try_push_priority(
            scheduler,
            bounded_normal_task,
            NULL,
            CORTLET_PRIORITY_HIGH
        );

    if (
        try_priority_result
        != CORTLET_QUEUE_FULL
    ) {
        fprintf(
            stderr,
            "FAIL: try_push_priority did not return CORTLET_QUEUE_FULL.\n"
        );

        atomic_store_explicit(
            &bounded_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       try_push_priority correctly returned QUEUE_FULL\n"
    );

    /*
     * Allow the worker to continue and drain the scheduler.
     */
    atomic_store_explicit(
        &bounded_gate_open,
        1,
        memory_order_release
    );

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: bounded scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &bounded_completed,
            memory_order_relaxed
        );

    printf(
        "       completed accepted tasks: %u/4\n",
        completed
    );

    if (completed != 4U) {
        fprintf(
            stderr,
            "FAIL: bounded scheduler completed wrong task count.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Once capacity is available again, try_push should succeed.
     */
    if (
        cortlet_sched_try_push(
            scheduler,
            bounded_normal_task,
            NULL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: try_push failed after capacity was released.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: wait after try_push failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    completed =
        atomic_load_explicit(
            &bounded_completed,
            memory_order_relaxed
        );

    if (completed != 5U) {
        fprintf(
            stderr,
            "FAIL: post-capacity try_push task did not complete.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Also verify the priority variant succeeds after capacity is free.
     */
    if (
        cortlet_sched_try_push_priority(
            scheduler,
            bounded_normal_task,
            NULL,
            CORTLET_PRIORITY_HIGH
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: try_push_priority failed after capacity was released.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: final priority try_push wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    completed =
        atomic_load_explicit(
            &bounded_completed,
            memory_order_relaxed
        );

    if (completed != 6U) {
        fprintf(
            stderr,
            "FAIL: post-capacity priority task did not complete.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       post-capacity try_push accepted new work\n"
    );

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: bounded queue capacity\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_concurrent_submission(void)
{
    printf(
        "[TEST] concurrent task submission\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 8,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: concurrent scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &concurrent_completed,
        0U,
        memory_order_relaxed
    );

    pthread_t threads[
        CONCURRENT_SUBMITTER_THREADS
    ];

    concurrent_submitter_args_t args[
        CONCURRENT_SUBMITTER_THREADS
    ];

    for (
        unsigned int i = 0;
        i < CONCURRENT_SUBMITTER_THREADS;
        ++i
    ) {
        args[i].scheduler =
            scheduler;

        args[i].task_count =
            TASKS_PER_SUBMITTER;

        args[i].failed =
            0;

        int result =
            pthread_create(
                &threads[i],
                NULL,
                concurrent_submitter_main,
                &args[i]
            );

        if (result != 0) {
            fprintf(
                stderr,
                "FAIL: pthread_create failed for thread %u.\n",
                i
            );

            for (
                unsigned int j = 0;
                j < i;
                ++j
            ) {
                pthread_join(
                    threads[j],
                    NULL
                );
            }

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    for (
        unsigned int i = 0;
        i < CONCURRENT_SUBMITTER_THREADS;
        ++i
    ) {
        if (
            pthread_join(
                threads[i],
                NULL
            )
            != 0
        ) {
            fprintf(
                stderr,
                "FAIL: pthread_join failed for thread %u.\n",
                i
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }

        if (args[i].failed) {
            fprintf(
                stderr,
                "FAIL: submission failed in thread %u.\n",
                i
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    const unsigned int expected =
        CONCURRENT_SUBMITTER_THREADS
        * TASKS_PER_SUBMITTER;

    unsigned int completed =
        atomic_load_explicit(
            &concurrent_completed,
            memory_order_relaxed
        );

    printf(
        "       submitters: %u\n",
        CONCURRENT_SUBMITTER_THREADS
    );

    printf(
        "       tasks per submitter: %u\n",
        TASKS_PER_SUBMITTER
    );

    printf(
        "       completed: %u/%u\n",
        completed,
        expected
    );

    if (completed != expected) {
        fprintf(
            stderr,
            "FAIL: concurrent task count mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: concurrent task submission\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_concurrent_bounded_capacity(void)
{
    printf(
        "[TEST] concurrent bounded capacity\n"
    );

    const unsigned int capacity = 16U;

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = capacity
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: bounded concurrent scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &bounded_concurrent_gate_open,
        0,
        memory_order_release
    );

    atomic_store_explicit(
        &bounded_concurrent_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &bounded_concurrent_accepted,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &bounded_concurrent_full,
        0U,
        memory_order_relaxed
    );

    pthread_t threads[
        BOUNDED_CONCURRENT_THREADS
    ];

    bounded_concurrent_args_t args = {
        .scheduler = scheduler
    };

    for (
        unsigned int i = 0;
        i < BOUNDED_CONCURRENT_THREADS;
        ++i
    ) {
        if (
            pthread_create(
                &threads[i],
                NULL,
                bounded_concurrent_submitter,
                &args
            )
            != 0
        ) {
            fprintf(
                stderr,
                "FAIL: pthread_create failed at thread %u.\n",
                i
            );

            atomic_store_explicit(
                &bounded_concurrent_gate_open,
                1,
                memory_order_release
            );

            for (
                unsigned int j = 0;
                j < i;
                ++j
            ) {
                pthread_join(
                    threads[j],
                    NULL
                );
            }

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    for (
        unsigned int i = 0;
        i < BOUNDED_CONCURRENT_THREADS;
        ++i
    ) {
        void *thread_result = NULL;

        if (
            pthread_join(
                threads[i],
                &thread_result
            )
            != 0
        ) {
            fprintf(
                stderr,
                "FAIL: pthread_join failed at thread %u.\n",
                i
            );

            atomic_store_explicit(
                &bounded_concurrent_gate_open,
                1,
                memory_order_release
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }

        if (thread_result != NULL) {
            fprintf(
                stderr,
                "FAIL: submitter received unexpected scheduler result.\n"
            );

            atomic_store_explicit(
                &bounded_concurrent_gate_open,
                1,
                memory_order_release
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    unsigned int accepted =
        atomic_load_explicit(
            &bounded_concurrent_accepted,
            memory_order_relaxed
        );

    unsigned int full =
        atomic_load_explicit(
            &bounded_concurrent_full,
            memory_order_relaxed
        );

    const unsigned int total_attempts =
        BOUNDED_CONCURRENT_THREADS
        * BOUNDED_CONCURRENT_ATTEMPTS;

    printf(
        "       capacity: %u\n",
        capacity
    );

    printf(
        "       accepted while blocked: %u\n",
        accepted
    );

    printf(
        "       queue-full responses: %u\n",
        full
    );

    if (accepted > capacity) {
        fprintf(
            stderr,
            "FAIL: accepted %u tasks with capacity %u.\n",
            accepted,
            capacity
        );

        atomic_store_explicit(
            &bounded_concurrent_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        accepted + full
        != total_attempts
    ) {
        fprintf(
            stderr,
            "FAIL: attempt accounting mismatch.\n"
        );

        atomic_store_explicit(
            &bounded_concurrent_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Because every accepted task blocks, capacity should reach
     * exactly the configured limit.
     */
    if (accepted != capacity) {
        fprintf(
            stderr,
            "FAIL: expected exactly %u accepted tasks, got %u.\n",
            capacity,
            accepted
        );

        atomic_store_explicit(
            &bounded_concurrent_gate_open,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Release accepted tasks.
     */
    atomic_store_explicit(
        &bounded_concurrent_gate_open,
        1,
        memory_order_release
    );

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: concurrent bounded wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &bounded_concurrent_completed,
            memory_order_relaxed
        );

    printf(
        "       completed: %u/%u\n",
        completed,
        accepted
    );

    if (completed != accepted) {
        fprintf(
            stderr,
            "FAIL: accepted/completed count mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: concurrent bounded capacity\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_push_and_try_group_priority(void)
{
    printf(
        "[TEST] push and try group priority\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 2,
        .pin_workers = 0,
        .queue_capacity = 16
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    cortlet_task_group_t *group =
        cortlet_task_group_create();

    if (group == NULL) {
        fprintf(
            stderr,
            "FAIL: task group creation failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &push_try_group_completed,
        0U,
        memory_order_relaxed
    );

    /*
     * 1. Normal grouped push.
     */
    if (
        cortlet_sched_push_group(
            scheduler,
            group,
            push_try_group_task,
            NULL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: push_group failed.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * 2. Explicit HIGH-priority grouped push.
     */
    if (
        cortlet_sched_push_group_priority(
            scheduler,
            group,
            push_try_group_task,
            NULL,
            CORTLET_PRIORITY_HIGH
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: push_group_priority failed.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * 3. Non-blocking NORMAL grouped push.
     */
    if (
        cortlet_sched_try_push_group(
            scheduler,
            group,
            push_try_group_task,
            NULL
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: try_push_group failed.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * 4. Non-blocking LOW-priority grouped push.
     */
    if (
        cortlet_sched_try_push_group_priority(
            scheduler,
            group,
            push_try_group_task,
            NULL,
            CORTLET_PRIORITY_LOW
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: try_push_group_priority failed.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Wait only for this group.
     */
    if (
        cortlet_task_group_wait(
            group
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: task group wait failed.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &push_try_group_completed,
            memory_order_relaxed
        );

    printf(
        "       completed grouped tasks: %u/4\n",
        completed
    );

    if (completed != 4U) {
        fprintf(
            stderr,
            "FAIL: expected 4 completed grouped tasks, got %u.\n",
            completed
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Invalid priority must still be rejected.
     */
    if (
        cortlet_sched_try_push_group_priority(
            scheduler,
            group,
            push_try_group_task,
            NULL,
            (cortlet_priority_t)99
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: invalid grouped try priority was accepted.\n"
        );

        cortlet_task_group_destroy(
            group
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       invalid priority correctly rejected\n"
    );

    cortlet_task_group_destroy(
        group
    );

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: push and try group priority\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_scheduler_statistics(void)
{
    printf(
        "[TEST] scheduler statistics\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 2,
        .pin_workers = 0,
        .queue_capacity = 4
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &stats_completed,
        0U,
        memory_order_relaxed
    );

    cortlet_sched_stats_t stats;

    /*
     * Initial statistics should all be zero.
     */
    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: initial get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        stats.submitted != 0U
        || stats.completed != 0U
        || stats.rejected_full != 0U
        || stats.outstanding != 0U
    ) {
        fprintf(
            stderr,
            "FAIL: initial statistics were not zero.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Submit four tasks.
     */
    for (
        unsigned int i = 0;
        i < 4U;
        ++i
    ) {
        if (
            cortlet_sched_push(
                scheduler,
                stats_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: stats task submission %u failed.\n",
                i
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Wait for all four accepted tasks.
     */
    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: final get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       submitted: %llu\n",
        (unsigned long long)stats.submitted
    );

    printf(
        "       completed: %llu\n",
        (unsigned long long)stats.completed
    );

    printf(
        "       rejected full: %llu\n",
        (unsigned long long)stats.rejected_full
    );

    printf(
        "       outstanding: %zu\n",
        stats.outstanding
    );

    printf(
        "       steals attempted: %llu\n",
        (unsigned long long)stats.steals_attempted
    );

    printf(
        "       steals succeeded: %llu\n",
        (unsigned long long)stats.steals_succeeded
    );

    if (stats.submitted != 4U) {
        fprintf(
            stderr,
            "FAIL: expected 4 submitted tasks.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.completed != 4U) {
        fprintf(
            stderr,
            "FAIL: expected 4 completed tasks.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.outstanding != 0U) {
        fprintf(
            stderr,
            "FAIL: expected 0 outstanding tasks.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &stats_completed,
            memory_order_relaxed
        );

    if (completed != 4U) {
        fprintf(
            stderr,
            "FAIL: callback completion count mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Dedicated invalid-argument checks for the statistics API.
     */
    if (
        cortlet_sched_get_stats(
            NULL,
            &stats
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats(NULL, stats) was not rejected.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_get_stats(
            scheduler,
            NULL
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats(scheduler, NULL) was not rejected.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: scheduler statistics\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_work_stealing_statistics(void)
{
    printf(
        "[TEST] work stealing statistics\n"
    );

    const unsigned int task_count = 50000U;

    cortlet_sched_config_t config = {
        .worker_count = 8,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &steal_stats_completed,
        0U,
        memory_order_relaxed
    );

    for (
        unsigned int i = 0;
        i < task_count;
        ++i
    ) {
        if (
            cortlet_sched_push(
                scheduler,
                steal_stats_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: task submission failed at %u.\n",
                i
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_stats_t stats;

    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &steal_stats_completed,
            memory_order_relaxed
        );

    printf(
        "       completed: %u/%u\n",
        completed,
        task_count
    );

    printf(
        "       steals attempted: %llu\n",
        (unsigned long long)stats.steals_attempted
    );

    printf(
        "       steals succeeded: %llu\n",
        (unsigned long long)stats.steals_succeeded
    );

    if (completed != task_count) {
        fprintf(
            stderr,
            "FAIL: task completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.steals_attempted == 0U) {
        fprintf(
            stderr,
            "FAIL: no steal attempts were recorded.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        stats.steals_succeeded
        > stats.steals_attempted
    ) {
        fprintf(
            stderr,
            "FAIL: successful steals exceeded attempted steals.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.submitted != task_count) {
        fprintf(
            stderr,
            "FAIL: submitted stats mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.completed != task_count) {
        fprintf(
            stderr,
            "FAIL: completed stats mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: work stealing statistics\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_explicit_shutdown(void)
{
    printf(
        "[TEST] explicit shutdown\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_is_stopped(
            scheduler
        )
        != 0
    ) {
        fprintf(
            stderr,
            "FAIL: new scheduler reported stopped.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Submit work before shutdown.
     */
    for (
        unsigned int i = 0;
        i < 1000U;
        ++i
    ) {
        if (
            cortlet_sched_push(
                scheduler,
                concurrent_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: pre-shutdown submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_shutdown(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: shutdown failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_is_stopped(
            scheduler
        )
        != 1
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler did not report stopped.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * New work must now be rejected.
     */
    if (
        cortlet_sched_push(
            scheduler,
            concurrent_task,
            NULL
        )
        != CORTLET_SCHEDULER_STOPPED
    ) {
        fprintf(
            stderr,
            "FAIL: push after shutdown was accepted.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_try_push(
            scheduler,
            concurrent_task,
            NULL
        )
        != CORTLET_SCHEDULER_STOPPED
    ) {
        fprintf(
            stderr,
            "FAIL: try_push after shutdown was accepted.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Already accepted work must still drain.
     */
    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: wait after shutdown failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Shutdown should be idempotent.
     */
    if (
        cortlet_sched_shutdown(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: second shutdown failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: explicit shutdown\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_priority_preemption(void)
{
    printf(
        "[TEST] priority preemption\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 1,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &priority_preempt_index,
        0U,
        memory_order_relaxed
    );

    /*
     * Queue several LOW-priority tasks first.
     */
    for (
        unsigned int i = 0;
        i < 8U;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                priority_preempt_task,
                (void *)(uintptr_t)(10U + i),
                CORTLET_PRIORITY_LOW
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: LOW priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Give the single worker time to begin LOW work.
     */
    for (
        volatile unsigned int delay = 0;
        delay < 1000000U;
        ++delay
    ) {
    }

    /*
     * Submit HIGH-priority work after LOW work already exists.
     */
    if (
        cortlet_sched_push_priority(
            scheduler,
            priority_preempt_task,
            (void *)(uintptr_t)200U,
            CORTLET_PRIORITY_HIGH
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: HIGH priority submission failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int count =
        atomic_load_explicit(
            &priority_preempt_index,
            memory_order_relaxed
        );

    printf(
        "       execution order:"
    );

    for (
        unsigned int i = 0;
        i < count;
        ++i
    ) {
        printf(
            " %u",
            priority_preempt_order[i]
        );
    }

    printf(
        "\n"
    );

    unsigned int high_index = count;

    for (
        unsigned int i = 0;
        i < count;
        ++i
    ) {
        if (
            priority_preempt_order[i]
            == 200U
        ) {
            high_index = i;
            break;
        }
    }

    if (high_index == count) {
        fprintf(
            stderr,
            "FAIL: HIGH priority task never executed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * HIGH should execute before all remaining LOW work drains.
     */
    if (high_index >= 8U) {
        fprintf(
            stderr,
            "FAIL: HIGH priority task did not preempt queued LOW work.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: priority preemption\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_priority_fairness(void)
{
    printf(
        "[TEST] priority fairness\n"
    );

    const unsigned int high_count = 50000U;
    const unsigned int low_count = 1000U;

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &fairness_high_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &fairness_low_completed,
        0U,
        memory_order_relaxed
    );

    /*
     * Queue LOW work first.
     */
    for (
        unsigned int i = 0;
        i < low_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                fairness_low_task,
                NULL,
                CORTLET_PRIORITY_LOW
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: LOW priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Flood the scheduler with HIGH work.
     */
    for (
        unsigned int i = 0;
        i < high_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                fairness_high_task,
                NULL,
                CORTLET_PRIORITY_HIGH
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: HIGH priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int high_completed =
        atomic_load_explicit(
            &fairness_high_completed,
            memory_order_relaxed
        );

    unsigned int low_completed =
        atomic_load_explicit(
            &fairness_low_completed,
            memory_order_relaxed
        );

    printf(
        "       HIGH completed: %u/%u\n",
        high_completed,
        high_count
    );

    printf(
        "       LOW completed: %u/%u\n",
        low_completed,
        low_count
    );

    if (high_completed != high_count) {
        fprintf(
            stderr,
            "FAIL: HIGH priority completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (low_completed != low_count) {
        fprintf(
            stderr,
            "FAIL: LOW priority work was starved.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: priority fairness\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_priority_progress_fairness(void)
{
    printf(
        "[TEST] priority progress fairness\n"
    );

    const unsigned int high_count = 20000U;
    const unsigned int low_count = 100U;

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &progress_high_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &progress_low_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &high_count_when_first_low_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &first_low_recorded,
        0,
        memory_order_relaxed
    );

    /*
     * Queue the LOW work first.
     */
    for (
        unsigned int i = 0;
        i < low_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                progress_low_task,
                NULL,
                CORTLET_PRIORITY_LOW
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: LOW priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Flood the scheduler with HIGH work.
     */
    for (
        unsigned int i = 0;
        i < high_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                progress_high_task,
                NULL,
                CORTLET_PRIORITY_HIGH
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: HIGH priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int high_completed =
        atomic_load_explicit(
            &progress_high_completed,
            memory_order_relaxed
        );

    unsigned int low_completed =
        atomic_load_explicit(
            &progress_low_completed,
            memory_order_relaxed
        );

    unsigned int high_at_first_low =
        atomic_load_explicit(
            &high_count_when_first_low_completed,
            memory_order_relaxed
        );

    printf(
        "       HIGH completed: %u/%u\n",
        high_completed,
        high_count
    );

    printf(
        "       LOW completed: %u/%u\n",
        low_completed,
        low_count
    );

    printf(
        "       HIGH completed when first LOW finished: %u/%u\n",
        high_at_first_low,
        high_count
    );

    if (
        high_completed
        != high_count
    ) {
        fprintf(
            stderr,
            "FAIL: HIGH completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        low_completed
        != low_count
    ) {
        fprintf(
            stderr,
            "FAIL: LOW completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * The important fairness property:
     *
     * At least one LOW task must finish before the entire HIGH backlog
     * has drained.
     */
    if (
        high_at_first_low
        >= high_count
    ) {
        fprintf(
            stderr,
            "FAIL: LOW priority work made no progress until all HIGH work completed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: priority progress fairness\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_priority_live_fairness(void)
{
    printf(
        "[TEST] priority live fairness\n"
    );

    const unsigned int high_count = 50000U;
    const unsigned int low_count = 100U;

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &live_high_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &live_low_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &live_high_at_first_low,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &live_first_low_recorded,
        0,
        memory_order_relaxed
    );

    /*
     * Queue a large HIGH backlog first.
     */
    for (
        unsigned int i = 0;
        i < high_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                live_high_task,
                NULL,
                CORTLET_PRIORITY_HIGH
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: HIGH priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Wait until HIGH work is definitely already executing.
     */
    while (
        atomic_load_explicit(
            &live_high_completed,
            memory_order_relaxed
        )
        < 100U
    ) {
    }

    unsigned int high_before_low =
        atomic_load_explicit(
            &live_high_completed,
            memory_order_relaxed
        );

    printf(
        "       HIGH completed before LOW injection: %u\n",
        high_before_low
    );

    /*
     * Inject LOW work while the HIGH backlog is still active.
     */
    for (
        unsigned int i = 0;
        i < low_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                live_low_task,
                NULL,
                CORTLET_PRIORITY_LOW
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: LOW priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int high_completed =
        atomic_load_explicit(
            &live_high_completed,
            memory_order_relaxed
        );

    unsigned int low_completed =
        atomic_load_explicit(
            &live_low_completed,
            memory_order_relaxed
        );

    unsigned int high_at_first_low =
        atomic_load_explicit(
            &live_high_at_first_low,
            memory_order_relaxed
        );

    cortlet_sched_stats_t stats;

    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       HIGH completed: %u/%u\n",
        high_completed,
        high_count
    );

    printf(
        "       LOW completed: %u/%u\n",
        low_completed,
        low_count
    );

    printf(
        "       HIGH completed when first LOW finished: %u/%u\n",
        high_at_first_low,
        high_count
    );

    printf(
        "       fairness yields: %llu\n",
        (unsigned long long)stats.fairness_yields
    );

    if (high_completed != high_count) {
        fprintf(
            stderr,
            "FAIL: HIGH completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (low_completed != low_count) {
        fprintf(
            stderr,
            "FAIL: LOW completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * LOW work must make progress before the entire HIGH backlog drains.
     */
    if (
        high_at_first_low
        >= high_count
    ) {
        fprintf(
            stderr,
            "FAIL: LOW work was starved until all HIGH work completed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: priority live fairness\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_fairness_yield_statistics(void)
{
    printf(
        "[TEST] fairness yield statistics\n"
    );

    const unsigned int high_count = 256U;
    const unsigned int low_count = 8U;

    cortlet_sched_config_t config = {
        .worker_count = 1,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &fairness_stats_high_completed,
        0U,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &fairness_stats_low_completed,
        0U,
        memory_order_relaxed
    );

    /*
     * Queue enough HIGH work to exceed HIGH_BURST_LIMIT.
     */
    for (
        unsigned int i = 0;
        i < high_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                fairness_stats_high_task,
                NULL,
                CORTLET_PRIORITY_HIGH
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: HIGH priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * Wait until HIGH work has definitely started.
     */
    while (
        atomic_load_explicit(
            &fairness_stats_high_completed,
            memory_order_relaxed
        )
        < 4U
    ) {
    }

    /*
     * Make LOW work available while HIGH work still remains.
     */
    for (
        unsigned int i = 0;
        i < low_count;
        ++i
    ) {
        if (
            cortlet_sched_push_priority(
                scheduler,
                fairness_stats_low_task,
                NULL,
                CORTLET_PRIORITY_LOW
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: LOW priority submission failed.\n"
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_stats_t stats;

    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int high_completed =
        atomic_load_explicit(
            &fairness_stats_high_completed,
            memory_order_relaxed
        );

    unsigned int low_completed =
        atomic_load_explicit(
            &fairness_stats_low_completed,
            memory_order_relaxed
        );

    printf(
        "       HIGH completed: %u/%u\n",
        high_completed,
        high_count
    );

    printf(
        "       LOW completed: %u/%u\n",
        low_completed,
        low_count
    );

    printf(
        "       fairness yields: %llu\n",
        (unsigned long long)stats.fairness_yields
    );

    if (high_completed != high_count) {
        fprintf(
            stderr,
            "FAIL: HIGH completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (low_completed != low_count) {
        fprintf(
            stderr,
            "FAIL: LOW completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (stats.fairness_yields == 0U) {
        fprintf(
            stderr,
            "FAIL: fairness mechanism never yielded to lower-priority work.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: fairness yield statistics\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_scheduler_introspection(void)
{
    printf(
        "[TEST] scheduler introspection\n"
    );

    const size_t worker_count = 4U;
    const unsigned int task_count = 4U;

    cortlet_sched_config_t config = {
        .worker_count = worker_count,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &introspection_gate,
        0,
        memory_order_release
    );

    atomic_store_explicit(
        &introspection_completed,
        0U,
        memory_order_relaxed
    );

    /*
     * Verify the configured fairness burst limit.
     */
    size_t burst_limit =
        cortlet_sched_high_burst_limit();

    printf(
        "       HIGH burst limit: %zu\n",
        burst_limit
    );

    if (burst_limit != 32U) {
        fprintf(
            stderr,
            "FAIL: expected HIGH burst limit 32, got %zu.\n",
            burst_limit
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Fresh scheduler should have no outstanding work.
     */
    size_t initial_outstanding =
        cortlet_sched_outstanding_tasks(
            scheduler
        );

    if (initial_outstanding != 0U) {
        fprintf(
            stderr,
            "FAIL: fresh scheduler has %zu outstanding tasks.\n",
            initial_outstanding
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Queue blocked tasks so outstanding state remains observable.
     */
    for (
        unsigned int i = 0;
        i < task_count;
        ++i
    ) {
        if (
            cortlet_sched_push(
                scheduler,
                introspection_blocking_task,
                NULL
            )
            != CORTLET_OK
        ) {
            fprintf(
                stderr,
                "FAIL: blocking task submission failed.\n"
            );

            atomic_store_explicit(
                &introspection_gate,
                1,
                memory_order_release
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    /*
     * All submitted tasks are blocked, so they should still be
     * counted as outstanding.
     */
    size_t outstanding =
        cortlet_sched_outstanding_tasks(
            scheduler
        );

    size_t sleeping =
        cortlet_sched_sleeping_workers(
            scheduler
        );

    printf(
        "       outstanding while blocked: %zu\n",
        outstanding
    );

    printf(
        "       sleeping workers: %zu/%zu\n",
        sleeping,
        worker_count
    );

    if (outstanding != task_count) {
        fprintf(
            stderr,
            "FAIL: expected %u outstanding tasks, got %zu.\n",
            task_count,
            outstanding
        );

        atomic_store_explicit(
            &introspection_gate,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * sleeping_workers is a live snapshot.
     *
     * The only strict invariant is that it must never exceed the
     * configured worker count.
     */
    if (sleeping > worker_count) {
        fprintf(
            stderr,
            "FAIL: sleeping worker count exceeded worker count.\n"
        );

        atomic_store_explicit(
            &introspection_gate,
            1,
            memory_order_release
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Release all blocked tasks.
     */
    atomic_store_explicit(
        &introspection_gate,
        1,
        memory_order_release
    );

    if (
        cortlet_sched_wait(
            scheduler
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: scheduler wait failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &introspection_completed,
            memory_order_relaxed
        );

    size_t final_outstanding =
        cortlet_sched_outstanding_tasks(
            scheduler
        );

    printf(
        "       completed: %u/%u\n",
        completed,
        task_count
    );

    printf(
        "       outstanding after wait: %zu\n",
        final_outstanding
    );

    if (completed != task_count) {
        fprintf(
            stderr,
            "FAIL: task completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (final_outstanding != 0U) {
        fprintf(
            stderr,
            "FAIL: scheduler still has outstanding tasks after wait.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * NULL introspection behavior.
     */
    if (
        cortlet_sched_outstanding_tasks(
            NULL
        )
        != 0U
    ) {
        fprintf(
            stderr,
            "FAIL: outstanding_tasks(NULL) did not return 0.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_sleeping_workers(
            NULL
        )
        != 0U
    ) {
        fprintf(
            stderr,
            "FAIL: sleeping_workers(NULL) did not return 0.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: scheduler introspection\n\n"
    );

    return EXIT_SUCCESS;
}

static int test_steal_failure_statistics(void)
{
    printf(
        "[TEST] steal failure statistics\n"
    );

    cortlet_sched_config_t config = {
        .worker_count = 4,
        .pin_workers = 0,
        .queue_capacity = 0
    };

    cortlet_sched_t *scheduler =
        cortlet_sched_init_ex(
            &config
        );

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "FAIL: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    /*
     * Give idle workers time to inspect peers.
     *
     * With no work queued, peer steal attempts should eventually
     * encounter empty victims and record failures.
     */
    for (
        volatile unsigned int delay = 0;
        delay < 5000000U;
        ++delay
    ) {
    }

    cortlet_sched_stats_t stats;

    if (
        cortlet_sched_get_stats(
            scheduler,
            &stats
        )
        != CORTLET_OK
    ) {
        fprintf(
            stderr,
            "FAIL: get_stats failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "       steals attempted: %llu\n",
        (unsigned long long)stats.steals_attempted
    );

    printf(
        "       steals succeeded: %llu\n",
        (unsigned long long)stats.steals_succeeded
    );

    printf(
        "       steal failures: %llu\n",
        (unsigned long long)stats.steal_failures
    );

    /*
     * There should be no successful steals because no tasks
     * were ever submitted.
     */
    if (stats.steals_succeeded != 0U) {
        fprintf(
            stderr,
            "FAIL: successful steals occurred with no submitted work.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Idle workers should eventually attempt peer steals.
     */
    if (stats.steals_attempted == 0U) {
        fprintf(
            stderr,
            "FAIL: no steal attempts were recorded.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Since every peer was empty, at least one failure should
     * have been recorded.
     */
    if (stats.steal_failures == 0U) {
        fprintf(
            stderr,
            "FAIL: no steal failures were recorded.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * A failure must correspond to an actual steal attempt.
     */
    if (
        stats.steal_failures
        > stats.steals_attempted
    ) {
        fprintf(
            stderr,
            "FAIL: steal failures exceeded steal attempts.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "PASS: steal failure statistics\n\n"
    );

    return EXIT_SUCCESS;
}

/*
 * ---------------------------------------------------------
 * Main
 * ---------------------------------------------------------
 */

int main(void)
{
#ifndef __linux__
    fprintf(
        stderr,
        "libcortlet-upgradesched v2 scheduler "
        "tests require Linux.\n"
    );

    return EXIT_FAILURE;
#endif

    printf(
        "libcortlet-upgradesched v2 scheduler tests\n"
    );

    printf(
        "=========================================\n\n"
    );

    if (
        test_creation()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_single_task()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_multiple_batches()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_repeated_wait()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_destroy_drains()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_invalid_arguments()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }
    if (
    test_custom_configuration()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}

    if (
        test_auto_worker_configuration()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_pinning_configuration()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    if (
        test_null_configuration()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }
    if (
    test_task_priorities()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
    }

    if (
    test_task_groups()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_destroy_group_with_pending_tasks()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_concurrent_submission()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_bounded_queue_capacity()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_concurrent_bounded_capacity()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_push_and_try_group_priority()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_scheduler_statistics()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_work_stealing_statistics()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_explicit_shutdown()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_priority_preemption()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_priority_fairness()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_priority_progress_fairness()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_priority_live_fairness()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_fairness_yield_statistics()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_scheduler_introspection()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}
    if (
    test_steal_failure_statistics()
    != EXIT_SUCCESS
) {
        return EXIT_FAILURE;
}

    printf(
        "=========================================\n"
    );

    printf(
        "All scheduler tests passed.\n"
    );

    return EXIT_SUCCESS;
}