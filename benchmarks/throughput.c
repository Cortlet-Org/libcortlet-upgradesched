#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cortlet-upgradesched.h"

static _Atomic uint64_t completed_tasks = 0;

static double now_seconds(void)
{
    struct timespec ts;

    if (
        clock_gettime(
            CLOCK_MONOTONIC,
            &ts
        )
        != 0
    ) {
        return 0.0;
    }

    return
        (double)ts.tv_sec
        + ((double)ts.tv_nsec / 1000000000.0);
}

static void benchmark_task(
    void *argument
)
{
    (void)argument;

    atomic_fetch_add_explicit(
        &completed_tasks,
        1U,
        memory_order_relaxed
    );
}

static int run_throughput_benchmark(
    size_t worker_count,
    uint64_t task_count
)
{
    printf(
        "libcortlet-upgradesched v2 throughput benchmark\n"
    );

    printf(
        "================================================\n\n"
    );

    printf(
        "workers: %zu\n",
        worker_count
    );

    printf(
        "tasks:   %" PRIu64 "\n\n",
        task_count
    );

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
            "ERROR: scheduler creation failed.\n"
        );

        return EXIT_FAILURE;
    }

    atomic_store_explicit(
        &completed_tasks,
        0U,
        memory_order_relaxed
    );

    /*
     * Measure submission separately.
     */
    double submit_start =
        now_seconds();

    for (
        uint64_t i = 0;
        i < task_count;
        ++i
    ) {
        cortlet_result_t result =
            cortlet_sched_push(
                scheduler,
                benchmark_task,
                NULL
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "ERROR: task submission failed at task %" PRIu64
                " with result %d.\n",
                i,
                (int)result
            );

            cortlet_sched_destroy(
                scheduler
            );

            return EXIT_FAILURE;
        }
    }

    double submit_end =
        now_seconds();

    /*
     * Measure how much additional time is required after
     * submission has completed.
     */
    double wait_start =
        now_seconds();

    cortlet_result_t wait_result =
        cortlet_sched_wait(
            scheduler
        );

    double wait_end =
        now_seconds();

    if (wait_result != CORTLET_OK) {
        fprintf(
            stderr,
            "ERROR: scheduler wait failed with result %d.\n",
            (int)wait_result
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    uint64_t completed =
        atomic_load_explicit(
            &completed_tasks,
            memory_order_relaxed
        );

    double submission_seconds =
        submit_end - submit_start;

    double wait_seconds =
        wait_end - wait_start;

    double total_seconds =
        wait_end - submit_start;

    double submission_rate =
        submission_seconds > 0.0
        ? (double)task_count / submission_seconds
        : 0.0;

    double total_throughput =
        total_seconds > 0.0
        ? (double)completed / total_seconds
        : 0.0;

    cortlet_sched_stats_t stats;

    cortlet_result_t stats_result =
        cortlet_sched_get_stats(
            scheduler,
            &stats
        );

    printf(
        "Results\n"
    );

    printf(
        "-------\n"
    );

    printf(
        "submitted:        %" PRIu64 "\n",
        task_count
    );

    printf(
        "completed:        %" PRIu64 "/%" PRIu64 "\n",
        completed,
        task_count
    );

    printf(
        "submission time:  %.6f s\n",
        submission_seconds
    );

    printf(
        "wait time:        %.6f s\n",
        wait_seconds
    );

    printf(
        "total time:       %.6f s\n",
        total_seconds
    );

    printf(
        "submission rate:  %.2f tasks/sec\n",
        submission_rate
    );

    printf(
        "throughput:       %.2f tasks/sec\n",
        total_throughput
    );

    if (stats_result == CORTLET_OK) {
        printf(
            "\nScheduler statistics\n"
        );

        printf(
            "--------------------\n"
        );

        printf(
            "stats submitted:    %" PRIu64 "\n",
            stats.submitted
        );

        printf(
            "stats completed:    %" PRIu64 "\n",
            stats.completed
        );

        printf(
            "rejected full:      %" PRIu64 "\n",
            stats.rejected_full
        );

        printf(
            "steals attempted:   %" PRIu64 "\n",
            stats.steals_attempted
        );

        printf(
            "steals succeeded:   %" PRIu64 "\n",
            stats.steals_succeeded
        );

        printf(
            "steal failures:     %" PRIu64 "\n",
            stats.steal_failures
        );

        printf(
            "fairness yields:    %" PRIu64 "\n",
            stats.fairness_yields
        );

        printf(
            "outstanding:        %zu\n",
            stats.outstanding
        );
    }

    int success = 1;

    if (completed != task_count) {
        fprintf(
            stderr,
            "\nERROR: completion mismatch.\n"
        );

        success = 0;
    }

    if (
        stats_result == CORTLET_OK
        && stats.submitted != task_count
    ) {
        fprintf(
            stderr,
            "\nERROR: scheduler submitted statistics mismatch.\n"
        );

        success = 0;
    }

    if (
        stats_result == CORTLET_OK
        && stats.completed != task_count
    ) {
        fprintf(
            stderr,
            "\nERROR: scheduler completed statistics mismatch.\n"
        );

        success = 0;
    }

    if (
        stats_result == CORTLET_OK
        && stats.outstanding != 0U
    ) {
        fprintf(
            stderr,
            "\nERROR: scheduler still has outstanding work.\n"
        );

        success = 0;
    }

    cortlet_sched_destroy(
        scheduler
    );

    printf(
        "\n%s\n",
        success
        ? "BENCHMARK PASS"
        : "BENCHMARK FAIL"
    );

    return
        success
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}

int main(
    int argc,
    char **argv
)
{
    /*
     * Defaults:
     *
     * worker_count = 0
     *     Scheduler automatically detects worker count.
     *
     * task_count = 1,000,000
     */
    size_t worker_count = 0;
    uint64_t task_count = 1000000ULL;

    if (argc >= 2) {
        char *end = NULL;

        unsigned long long parsed =
            strtoull(
                argv[1],
                &end,
                10
            );

        if (
            end == argv[1]
            || *end != '\0'
        ) {
            fprintf(
                stderr,
                "Usage: %s [task_count] [worker_count]\n",
                argv[0]
            );

            return EXIT_FAILURE;
        }

        task_count =
            (uint64_t)parsed;
    }

    if (argc >= 3) {
        char *end = NULL;

        unsigned long long parsed =
            strtoull(
                argv[2],
                &end,
                10
            );

        if (
            end == argv[2]
            || *end != '\0'
        ) {
            fprintf(
                stderr,
                "Usage: %s [task_count] [worker_count]\n",
                argv[0]
            );

            return EXIT_FAILURE;
        }

        worker_count =
            (size_t)parsed;
    }

    if (task_count == 0U) {
        fprintf(
            stderr,
            "ERROR: task_count must be greater than 0.\n"
        );

        return EXIT_FAILURE;
    }

    return run_throughput_benchmark(
        worker_count,
        task_count
    );
}