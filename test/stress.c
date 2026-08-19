#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

#include "cortlet-upgradesched.h"

#define STRESS_TASK_COUNT 1000000UL

static atomic_ulong completed_tasks = 0;
static atomic_ullong checksum = 0;

static double elapsed_seconds(
    const struct timespec *start,
    const struct timespec *end
)
{
    double seconds =
        (double)(end->tv_sec - start->tv_sec);

    double nanoseconds =
        (double)(end->tv_nsec - start->tv_nsec)
        / 1000000000.0;

    return seconds + nanoseconds;
}

static void stress_task(void *argument)
{
    uintptr_t value = (uintptr_t)argument;

    /*
     * Small deterministic workload.
     *
     * This gives each scheduled task a little work while keeping
     * scheduler overhead highly visible.
     */
    unsigned long long result =
        ((unsigned long long)value * 2654435761ULL)
        ^ 0x9E3779B97F4A7C15ULL;

    atomic_fetch_add_explicit(
        &checksum,
        result,
        memory_order_relaxed
    );

    atomic_fetch_add_explicit(
        &completed_tasks,
        1,
        memory_order_relaxed
    );
}

static int run_stress_test(void)
{
    struct timespec total_start;
    struct timespec submit_end;
    struct timespec total_end;

    printf(
        "libcortlet-upgradesched v2 stress test\n"
    );

    printf(
        "Tasks: %lu\n",
        STRESS_TASK_COUNT
    );

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "ERROR: scheduler initialization failed.\n"
        );

        return EXIT_FAILURE;
    }

    size_t workers =
        cortlet_sched_worker_count(
            scheduler
        );

    printf(
        "Workers: %zu\n",
        workers
    );

    if (workers == 0) {
        fprintf(
            stderr,
            "ERROR: scheduler reported zero workers.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &completed_tasks,
        0,
        memory_order_relaxed
    );

    atomic_store_explicit(
        &checksum,
        0,
        memory_order_relaxed
    );

    if (
        clock_gettime(
            CLOCK_MONOTONIC,
            &total_start
        ) != 0
    ) {
        perror("clock_gettime");

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    /*
     * Submit all tasks.
     */
    for (
        uintptr_t i = 0;
        i < STRESS_TASK_COUNT;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_push(
                scheduler,
                stress_task,
                (void *)i
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "ERROR: task submission failed at %lu "
                "with result %d.\n",
                (unsigned long)i,
                (int)result
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    if (
        clock_gettime(
            CLOCK_MONOTONIC,
            &submit_end
        ) != 0
    ) {
        perror("clock_gettime");

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    printf(
        "All tasks submitted.\n"
    );

    /*
     * Wait for all outstanding work.
     */
    cortlet_result_t wait_result =
        cortlet_sched_wait(
            scheduler
        );

    if (wait_result != CORTLET_OK) {
        fprintf(
            stderr,
            "ERROR: cortlet_sched_wait() failed "
            "with result %d.\n",
            (int)wait_result
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    if (
        clock_gettime(
            CLOCK_MONOTONIC,
            &total_end
        ) != 0
    ) {
        perror("clock_gettime");

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

    unsigned long long final_checksum =
        atomic_load_explicit(
            &checksum,
            memory_order_relaxed
        );

    double submission_time =
        elapsed_seconds(
            &total_start,
            &submit_end
        );

    double wait_time =
        elapsed_seconds(
            &submit_end,
            &total_end
        );

    double total_time =
        elapsed_seconds(
            &total_start,
            &total_end
        );

    double throughput =
        total_time > 0.0
            ? (double)STRESS_TASK_COUNT
                / total_time
            : 0.0;

    printf(
        "Completed: %lu/%lu\n",
        completed,
        STRESS_TASK_COUNT
    );

    printf(
        "Checksum: %llu\n",
        final_checksum
    );

    printf(
        "\nTiming\n"
    );

    printf(
        "Submission time: %.6f seconds\n",
        submission_time
    );

    printf(
        "Wait time:       %.6f seconds\n",
        wait_time
    );

    printf(
        "Total time:      %.6f seconds\n",
        total_time
    );

    printf(
        "Throughput:      %.2f tasks/sec\n",
        throughput
    );

    if (completed != STRESS_TASK_COUNT) {
        fprintf(
            stderr,
            "ERROR: task completion mismatch.\n"
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
        "\nStress test passed.\n"
    );

    return EXIT_SUCCESS;
}

int main(void)
{
#ifndef __linux__
    fprintf(
        stderr,
        "ERROR: libcortlet-upgradesched v2 "
        "stress tests require Linux.\n"
    );

    return EXIT_FAILURE;
#endif

    return run_stress_test();
}