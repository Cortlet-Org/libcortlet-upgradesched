#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>

#include "cortlet-upgradesched.h"

#define TASK_COUNT 1000

static atomic_uint completed_tasks = 0;

static void test_task(void *argument)
{
    uintptr_t value = (uintptr_t) argument;

    /*
     * Do a tiny amount of deterministic work.
     */
    volatile uintptr_t result = value * value;
    (void) result;

    atomic_fetch_add_explicit(
        &completed_tasks,
        1,
        memory_order_relaxed
    );
}

static int test_version(void)
{
    uint32_t major = cortlet_version_major();
    uint32_t minor = cortlet_version_minor();
    uint32_t patch = cortlet_version_patch();

    printf(
        "libcortlet-upgradesched %u.%u.%u\n",
        major,
        minor,
        patch
    );

    if (major != CORTLET_VERSION_MAJOR) {
        fprintf(
            stderr,
            "Version major mismatch.\n"
        );

        return EXIT_FAILURE;
    }

    if (minor != CORTLET_VERSION_MINOR) {
        fprintf(
            stderr,
            "Version minor mismatch.\n"
        );

        return EXIT_FAILURE;
    }

    if (patch != CORTLET_VERSION_PATCH) {
        fprintf(
            stderr,
            "Version patch mismatch.\n"
        );

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int test_scheduler(void)
{
    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "Failed to create scheduler.\n"
        );

        return EXIT_FAILURE;
    }

    size_t worker_count =
        cortlet_sched_worker_count(
            scheduler
        );

    printf(
        "Workers: %zu\n",
        worker_count
    );

    if (worker_count == 0) {
        fprintf(
            stderr,
            "Scheduler reported zero workers.\n"
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

    for (uintptr_t i = 0;
         i < TASK_COUNT;
         ++i)
    {
        cortlet_result_t result =
            cortlet_sched_push(
                scheduler,
                test_task,
                (void *) i
            );

        if (result != CORTLET_OK) {
            fprintf(
                stderr,
                "Task submission failed at task %lu with result %d.\n",
                (unsigned long) i,
                (int) result
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
            "Scheduler wait failed with result %d.\n",
            (int) wait_result
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    unsigned int completed =
        atomic_load_explicit(
            &completed_tasks,
            memory_order_relaxed
        );

    printf(
        "Completed tasks: %u/%d\n",
        completed,
        TASK_COUNT
    );

    if (completed != TASK_COUNT) {
        fprintf(
            stderr,
            "Task completion mismatch.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    return EXIT_SUCCESS;
}

static int test_invalid_arguments(void)
{
    if (
        cortlet_sched_worker_count(NULL)
        != 0
    ) {
        fprintf(
            stderr,
            "NULL worker count test failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_wait(NULL)
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "NULL wait test failed.\n"
        );

        return EXIT_FAILURE;
    }

    if (
        cortlet_sched_push(
            NULL,
            test_task,
            NULL
        )
        != CORTLET_INVALID_ARGUMENT
    ) {
        fprintf(
            stderr,
            "NULL scheduler push test failed.\n"
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        fprintf(
            stderr,
            "Failed to create scheduler for invalid argument test.\n"
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
            "NULL task test failed.\n"
        );

        cortlet_sched_destroy(
            scheduler
        );

        return EXIT_FAILURE;
    }

    cortlet_sched_destroy(
        scheduler
    );

    return EXIT_SUCCESS;
}

int main(void)
{
#ifdef __linux__
    printf(
        "Platform: Linux\n"
    );
#else
    fprintf(
        stderr,
        "This test is intended for Linux.\n"
    );

    return EXIT_FAILURE;
#endif

    if (
        test_version()
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
        test_scheduler()
        != EXIT_SUCCESS
    ) {
        return EXIT_FAILURE;
    }

    printf(
        "All basic tests passed.\n"
    );

    return EXIT_SUCCESS;
}