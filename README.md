# libcortlet-upgradesched

An ultra-lightweight, zero-bloat, secure-first task scheduling library for high-performance Linux applications. Written in pure C11 with zero external dependencies, it acts as a drop-in execution engine designed to extract maximum performance from modern multi-core architectures.

---

## The Problem
Modern Linux applications processing heavy parallel workloads often suffer from severe performance degradation due to user-space scheduling inefficiencies. Standard multi-threading solutions typically introduce three critical bottlenecks:

1. **Thread Thrashing & Cache Invalidations:** The Linux kernel frequently context-switches threads across different physical CPU sockets, repeatedly flushing L1/L2 caches and introducing immense memory latency.
2. **Synchronization Overhead:** Traditional thread pools rely on heavy kernel-space mutexes, forcing execution threads to waste cycles waiting in sleep queues rather than crunching data.
3. **Resource Starvation or Crashes:** Massive spikes in task ingestion routinely cause uncontrolled memory allocation spikes, resulting in system instability or sudden process terminations via the Linux OOM (Out of Memory) Killer.

---

## Core Features & Architecture

### 1. Topology-Aware Hardware Binding
Upon initialization, the library reads the host machine's hardware topology and creates exactly one dedicated OS worker thread per physical CPU core. Each thread is strictly pinned to its respective core using hardware affinity masks, entirely eliminating cross-core thread bouncing and preserving L1/L2 cache locality.

### 2. Lock-Free Work-Stealing Protocol
Built natively on a Single-Producer Multi-Consumer (SPMC) ring buffer configuration using explicit atomic memory order barriers. Worker threads pull work instantly from their localized queues. If a core runs completely out of tasks, it safely pivots to steal tasks from the head of overloaded sibling queues using hardware-level atomic Compare-and-Swap (CAS) instructions.

### 3. Yield-Based Backoff Flow Control
To prevent application crashes during massive spikes in data ingestion, the library implements a bounded, static allocation footprint. When local task buffers fill to their absolute threshold, the producer thread automatically yields its execution time slice back to the OS scheduler, naturally throttling input speed to match physical processing limits without dropping payloads.

### 4. Zero-Overhead Thread Parking
To respect system purity and power efficiency, idle worker threads do not engage in hot spinlocks. When the entire system runs dry of work and stealing attempts fail, threads drop into a deep sleep, reducing the library's idle CPU utilization to exactly 0% until a new task broadcast wakes them up.

---

## System Specs & Requirements
* **Language Target:** Pure C11 (compiled with `-std=c11`).
* **Dependencies:** None (`glibc` / `musl` and `pthread` only).
* **Binary Footprint:** Microscopic (under 50 KB stripped shared object file).
* **Platform Support:** Linux (Kernel 4.x+ recommended for optimal scheduling topology mappings).

## Installation

### Prerequisites
`libcortlet-upgradesched` requires a C compiler (`gcc` or `clang`), `make`, and a standard C library supporting POSIX threads (`glibc` or `musl`).

Install the required build tools for your specific Linux distribution:

```bash
# On Debian / Ubuntu
sudo apt update && sudo apt install build-essential git

# On Fedora / RHEL / CentOS
sudo dnf install gcc make git

# On Arch Linux
sudo pacman -Syu base-devel git
```

## System-wide build & install
Once you installed the prerequisites, clone the repository and compile the binary directly onto your local system paths:
```bash
git clone https://github.com/cortlet/libcortlet-upgradesched.git
cd libcortlet-upgradesched
make
sudo make install
```
This automatically builds the shared (`.so`), and static (`.a`) targets, copies the binaries and public headers to your local system directories (`/usr/local/lib` and `/usr/local/include`), and refresh your system's dynamic linker configs (`ldconfig`)

## Usage

> <img src="https://raw.githubusercontent.com/primer/octicons/main/icons/info-16.svg" width="16" height="16" alt="info" style="display: inline-block; vertical-align: middle;"> NOTE: For more extensive details, implementation notes, and edge-case handling, refer to the full documentation at https://cortlet.com/docs/libcortlet-upgradesched

### Short API Reference

| Function Signature                                                                | Description                                                                                            | Return Value                                        |
|:----------------------------------------------------------------------------------|:-------------------------------------------------------------------------------------------------------|:----------------------------------------------------|
| `cortlet_sched_t* cortlet_sched_init(void)`                                       | Queries host hardware topology and initializes an isolated, core-pinned worker pool.                   | Pointer to scheduler context, or `NULL` on failure. |
| `int cortlet_sched_push(cortlet_sched_t* sched, cortlet_task_fn func, void* arg)` | Dispatches a task to a core queue via lock-free round-robin routing. Yields if buffers are full.       | `0` on success, `-1` on invalid arguments.          |
| `void cortlet_sched_wait(cortlet_sched_t* sched)`                                 | Blocks the calling thread until all outstanding tasks across all core queues reach zero.               | `void`                                              |
| `void cortlet_sched_destroy(cortlet_sched_t* sched)`                              | Signals worker threads to exit, joins execution tracks, and releases all internal tracking structures. | `void`                                              |

---

### Minimal Example

Below is a complete, drop-in example demonstrating how to orchestrate parallel problem-solving workloads across your machine's physical infrastructure.

```c
#include <cortlet-upgradesched.h>
#include <stdio.h>

// User-defined task matching the cortlet_task_fn signature
void compute_bound_worker(void* arg) {
    long task_id = (long)arg;
    printf("Processing task #%ld on dedicated physical silicon layout.\n", task_id);
}

int main(void) {
    // 1. Fire up the hardware-bound threading context
    cortlet_sched_t* sched = cortlet_sched_init();
    if (!sched) {
        fprintf(stderr, "Failed to map core execution matrix.\n");
        return 1;
    }

    // 2. Safely flood the lock-free pipeline with concurrent workloads 
    for (long i = 1; i <= 1000; i++) {
        cortlet_sched_push(sched, compute_bound_worker, (void*)i);
    }

    // 3. Sync barrier: Wait for everything to clear out safely
    cortlet_sched_wait(sched);

    // 4. Gracefully teardown threads and release memory allocations
    cortlet_sched_destroy(sched);

    return 0;
}
```

### Compilation & Linking
To build your application against the system-wide dynamic library, pass the library identifier and the native thread engine flags to gcc:

```bash
gcc main.c -lcortlet-upgradesched -lpthread -o parallel_app
./parallel_app
```

> NOTE: If you are building locally against the source tree instead of installing system-wide, add the target paths to your compilation command: `gcc main.c -Iinclude -Lbuild -lcortlet-upgradesched -lpthread -o parallel_app` or simply configure a `Makefile` and run `make`.
