# libcortlet-upgradesched

`libcortlet-upgradesched` is a Rust-powered task scheduler with a stable C ABI.

Version 2 focuses on fast task submission, work stealing, task priorities, bounded capacity, task groups, scheduler statistics, graceful shutdown, fairness, and Linux worker topology support.

## Version

Current release:

**v2.0.0**

## Features

- Rust scheduler core
- Stable C ABI
- C and C++ compatible public header
- Multi-worker scheduling
- Automatic worker-count detection
- Optional CPU worker pinning
- Concurrent task submission
- Work stealing
- Round-robin victim selection
- Task priorities
    - LOW
    - NORMAL
    - HIGH
- Priority preemption
- Priority fairness
- HIGH-priority burst limiting
- Task groups
- Group-specific waiting
- Safe task-group lifetime handling
- Bounded outstanding-task capacity
- Non-blocking `try_push` APIs
- Graceful scheduler shutdown
- Drain-on-destroy behavior
- Scheduler statistics
- Scheduler introspection
- ABI layout validation
- Stress tests
- Throughput benchmark

## Scheduler Statistics

The scheduler exposes runtime statistics through `cortlet_sched_get_stats()`.

Available statistics include:

- Submitted tasks
- Completed tasks
- Rejected submissions caused by full capacity
- Work-steal attempts
- Successful work steals
- Failed work steals
- Priority fairness activations
- Outstanding tasks

Example structure:

```c
typedef struct cortlet_sched_stats {
    uint64_t submitted;
    uint64_t completed;
    uint64_t rejected_full;
    uint64_t steals_attempted;
    uint64_t steals_succeeded;
    uint64_t steal_failures;
    uint64_t fairness_yields;
    size_t outstanding;
} cortlet_sched_stats_t;
```

Statistics are snapshots and may change immediately while the scheduler is active.

## Task Priorities

Three task priorities are supported:

```c
typedef enum cortlet_priority {
    CORTLET_PRIORITY_LOW = 0,
    CORTLET_PRIORITY_NORMAL = 1,
    CORTLET_PRIORITY_HIGH = 2
} cortlet_priority_t;
```

The scheduler prefers HIGH-priority work, then NORMAL, then LOW.

To prevent lower-priority starvation, workers use a fairness mechanism with a HIGH-priority burst limit.

Current burst limit:

```text
32 HIGH-priority selections
```

After the burst limit is reached, the worker checks lower-priority work before continuing with HIGH-priority tasks.

## Task Groups

Task groups allow related tasks to be tracked and waited on independently.

Example:

```c
cortlet_task_group_t *group =
    cortlet_task_group_create();

cortlet_sched_push_group(
    scheduler,
    group,
    task_function,
    argument
);

cortlet_task_group_wait(group);

cortlet_task_group_destroy(group);
```

Destroying the public group handle does not invalidate already-submitted tasks.

The scheduler keeps internal references until pending grouped tasks complete.

## Bounded Capacity

Schedulers can limit the number of outstanding tasks.

```c
cortlet_sched_config_t config = {
    .worker_count = 4,
    .pin_workers = 0,
    .queue_capacity = 1024
};
```

`queue_capacity` includes:

- Queued tasks
- Currently executing tasks

A value of `0` means unlimited capacity.

When capacity is full, non-blocking submission functions return:

```c
CORTLET_QUEUE_FULL
```

## Scheduler Configuration

```c
typedef struct cortlet_sched_config {
    size_t worker_count;
    int pin_workers;
    size_t queue_capacity;
} cortlet_sched_config_t;
```

### worker_count

- `0` = automatically detect worker count
- Non-zero = explicit worker thread count

### pin_workers

- `0` = CPU pinning disabled
- Non-zero = CPU pinning enabled

### queue_capacity

- `0` = unlimited
- Non-zero = maximum number of outstanding tasks

## Scheduler Lifecycle

Create a scheduler:

```c
cortlet_sched_t *scheduler =
    cortlet_sched_init();
```

Or use a custom configuration:

```c
cortlet_sched_config_t config = {
    .worker_count = 4,
    .pin_workers = 1,
    .queue_capacity = 0
};

cortlet_sched_t *scheduler =
    cortlet_sched_init_ex(&config);
```

Wait for all tasks:

```c
cortlet_sched_wait(scheduler);
```

Begin graceful shutdown:

```c
cortlet_sched_shutdown(scheduler);
```

Check shutdown state:

```c
int stopped =
    cortlet_sched_is_stopped(scheduler);
```

Destroy the scheduler:

```c
cortlet_sched_destroy(scheduler);
```

Destroying the scheduler drains already-accepted work before worker threads exit.

## Task Submission

Submit a NORMAL-priority task:

```c
cortlet_sched_push(
    scheduler,
    task_function,
    argument
);
```

Submit with explicit priority:

```c
cortlet_sched_push_priority(
    scheduler,
    task_function,
    argument,
    CORTLET_PRIORITY_HIGH
);
```

Try to submit without waiting for capacity:

```c
cortlet_sched_try_push(
    scheduler,
    task_function,
    argument
);
```

Priority-aware try-push:

```c
cortlet_sched_try_push_priority(
    scheduler,
    task_function,
    argument,
    CORTLET_PRIORITY_LOW
);
```

Grouped task submission is also supported:

```c
cortlet_sched_push_group(
    scheduler,
    group,
    task_function,
    argument
);
```

```c
cortlet_sched_push_group_priority(
    scheduler,
    group,
    task_function,
    argument,
    CORTLET_PRIORITY_HIGH
);
```

```c
cortlet_sched_try_push_group(
    scheduler,
    group,
    task_function,
    argument
);
```

```c
cortlet_sched_try_push_group_priority(
    scheduler,
    group,
    task_function,
    argument,
    CORTLET_PRIORITY_NORMAL
);
```

## Scheduler Introspection

The scheduler exposes lightweight live-state information.

Current outstanding task count:

```c
size_t outstanding =
    cortlet_sched_outstanding_tasks(scheduler);
```

Current sleeping worker count:

```c
size_t sleeping =
    cortlet_sched_sleeping_workers(scheduler);
```

Configured HIGH-priority burst limit:

```c
size_t burst_limit =
    cortlet_sched_high_burst_limit();
```

These values are live snapshots and may change immediately.

## Result Codes

```c
typedef enum cortlet_result {
    CORTLET_OK = 0,
    CORTLET_INVALID_ARGUMENT = 1,
    CORTLET_ALLOCATION_FAILED = 2,
    CORTLET_SCHEDULER_STOPPED = 3,
    CORTLET_QUEUE_FULL = 4,
    CORTLET_INTERNAL_ERROR = 255
} cortlet_result_t;
```

## Work Stealing

Workers use peer work stealing when:

- Their local queue is empty
- Global priority queues have no immediately available work

Version 2 uses round-robin victim selection.

Work-stealing statistics include:

- `steals_attempted`
- `steals_succeeded`
- `steal_failures`

Adaptive victim selection is intentionally reserved for a future major version.

## Throughput Benchmark

The project includes:

```text
benchmarks/throughput.c
```

Build benchmarks with:

```bash
cmake -S . -B cmake-build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCORTLET_BUILD_BENCHMARKS=ON
```

Build:

```bash
cmake --build cmake-build-release
```

Run:

```bash
./cmake-build-release/cortlet_benchmark_throughput
```

Custom task count:

```bash
./cmake-build-release/cortlet_benchmark_throughput 5000000
```

Custom task count and worker count:

```bash
./cmake-build-release/cortlet_benchmark_throughput 5000000 8
```

## Debug Benchmark Baseline

A 1,000,000-task Debug benchmark produced approximately:

```text
submission time:  0.242542 s
wait time:        0.000167 s
total time:       0.242709 s
submission rate:  4,122,993 tasks/sec
throughput:       4,120,159 tasks/sec
```

Scheduler statistics:

```text
submitted:         1,000,000
completed:         1,000,000
rejected full:     0
steals attempted:  902,748
steals succeeded:  33,473
steal failures:    859,549
fairness yields:   0
outstanding:       0
```

Release-build performance should be benchmarked separately.

## Tests

The v2 scheduler test suite covers:

- Scheduler creation
- Single-task execution
- Multiple task batches
- Repeated waits
- Drain-on-destroy
- Invalid arguments
- Custom configuration
- Automatic worker configuration
- CPU pinning configuration
- NULL configuration handling
- Task priorities
- Task groups
- Task-group destruction with pending work
- Concurrent task submission
- Bounded capacity
- Concurrent bounded capacity
- Grouped push and try-push APIs
- Scheduler statistics
- Work-stealing statistics
- Explicit shutdown
- Priority preemption
- Priority fairness
- Priority progress fairness
- Priority live fairness
- Fairness-yield statistics
- Scheduler introspection
- Steal-failure statistics

Run the test suite with:

```bash
ctest --test-dir cmake-build-release --output-on-failure
```

## Building

Requirements:

- Linux
- CMake 3.20+
- Rust toolchain
- Cargo
- C11 compiler
- pthread
- libdl
- libm

Configure:

```bash
cmake -S . -B cmake-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCORTLET_BUILD_TESTS=ON \
  -DCORTLET_BUILD_BENCHMARKS=ON
```

Build:

```bash
cmake --build cmake-build -j
```

## Installation

Install using CMake:

```bash
cmake --install cmake-build
```

The public header is installed as:

```text
include/cortlet-upgradesched.h
```

The Rust static library is installed into the platform library directory.

## Example

```c
#include <stdio.h>

#include <cortlet-upgradesched.h>

static void hello_task(void *argument)
{
    const char *message =
        argument;

    printf(
        "%s\n",
        message
    );
}

int main(void)
{
    cortlet_sched_t *scheduler =
        cortlet_sched_init();

    if (scheduler == NULL) {
        return 1;
    }

    if (
        cortlet_sched_push(
            scheduler,
            hello_task,
            "Hello from Cortlet"
        )
        != CORTLET_OK
    ) {
        cortlet_sched_destroy(
            scheduler
        );

        return 1;
    }

    cortlet_sched_wait(
        scheduler
    );

    cortlet_sched_destroy(
        scheduler
    );

    return 0;
}
```

## Thread Safety

Task submission is designed to support concurrent producer threads.

Scheduler operations such as task submission, waiting, statistics reads, and introspection may be used concurrently where documented.

`cortlet_sched_destroy()` must not race with another operation using the same scheduler handle.

After `cortlet_sched_destroy()` returns, the scheduler pointer is invalid.

The same lifetime rule applies to public task-group handles.

## ABI

The C interface uses `#[repr(C)]` Rust structures where data crosses the FFI boundary.

The project includes compile-time ABI layout checks for public structures such as:

- `cortlet_sched_config_t`
- `cortlet_sched_stats_t`

Field order must remain identical between the Rust and C definitions.

Changing public structure layout, enum representation, or function signatures may constitute an ABI-breaking change.

## Project Structure

```text
libcortlet-upgradesched/
├── benchmarks/
│   └── throughput.c
├── include/
│   └── cortlet-upgradesched.h
├── src/
│   ├── ffi.rs
│   ├── group.rs
│   ├── lib.rs
│   ├── queue.rs
│   ├── scheduler.rs
│   ├── stats.rs
│   ├── steal.rs
│   ├── topology.rs
│   └── worker.rs
├── test/
│   ├── basic.c
│   ├── scheduler.c
│   └── stress.c
├── Cargo.toml
├── Cargo.lock
└── CMakeLists.txt
```

## v2 Design Goals

Version 2 focuses on:

- Predictable scheduling behavior
- Fast concurrent submission
- Safe C interoperability
- Priority-aware execution
- Lower-priority starvation protection
- Observable scheduler behavior
- Graceful lifecycle management
- Stable task-group semantics
- Bounded workloads
- Strong test coverage

## Future Work

Features intentionally reserved for later versions include:

- Adaptive victim selection
- NUMA-aware scheduling
- More advanced CPU topology awareness
- Dynamic priority tuning
- Additional work-stealing policies
- More advanced scheduler heuristics

These are not required for the v2 scheduling model.

## License

See the repository license for licensing information.