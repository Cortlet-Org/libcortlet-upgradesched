#define _GNU_SOURCE
#include "../include/cortlet-upgradesched.h"
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

// Simple heavy-lifting structural simulation
void sample_problem_solver(void* arg) {
    long task_id = (long)arg;

    // Query the actual Linux kernel to see what physical core we are on
    int actual_core = sched_getcpu();

    printf("[Cortlet Worker] Executing task #%ld on physical CPU core: %d\n", task_id, actual_core);

    // Simulate minor computational workload latency
    usleep(50000);
}

int main(void) {
    printf("=== Initializing Cortlet UpgradeSched (v1 Test) ===\n");

    cortlet_sched_t* sched = cortlet_sched_init();
    if (!sched) {
        fprintf(stderr, "Failed to initialize scheduler.\n");
        return 1;
    }

    printf("\n=== Dispatching 50,000 Tasks via Lock-Free Pipeline ===\n");
    for (long i = 1; i <= 50000; i++) {
        cortlet_sched_push(sched, sample_problem_solver, (void*)i);
    }

    printf("\n=== Synchronization Barrier: Waiting for tasks to clear ===\n");
    cortlet_sched_wait(sched);

    printf("\n=== Tearing Down Infrastructure cleanly ===\n");
    cortlet_sched_destroy(sched);

    printf("Test completed successfully.\n");
    return 0;
}
