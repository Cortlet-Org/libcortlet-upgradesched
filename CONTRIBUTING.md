# Contributing to libcortlet-upgradesched

Thank you for your interest in making `libcortlet-upgradesched` faster, tighter, and more secure. We welcome contributions that push the limits of low-level Linux systems engineering.

Because this library handles raw lock-free concurrency, atomic memory ordering, and hardware affinity tracking, our engineering bar is exceptionally high. A single misplaced barrier or memory leak can compromise the entire scheduling matrix.

---

## Code of Conduct

Before participating, please review our official standards of engagement:
👉 **[Official Cortlet Code of Conduct](https://www.cortlet.com/legal/code-of-conduct)**

---

## Technical Standards & Guarantees

Every Pull Request must strictly conform to these architectural guardrails:

### 1. Pure C11 Specification
* No third-party dependencies are permitted. The library must compile purely against `glibc` or `musl` using standard POSIX interfaces (`pthread`, `sched`).
* All code must be strictly C11 compliant (`-std=c11`).

### 2. Lock-Free and Atomic Integrity
* Do not introduce standard kernel-level locks (`pthread_mutex_t`) into the hot execution paths (`queue_pop`, `queue_steal`, `cortlet_sched_push`).
* Any mutation to shared state must use explicit atomic memory orders (`memory_order_acquire`, `memory_order_release`, `memory_order_relaxed`).
* Never use sequential consistency (`memory_order_seq_cst`) as a lazy fix for a race condition—diagnose the boundary alignment instead.

### 3. Absolute Memory Safety
* Dynamic allocations must be kept out of hot processing paths. Use static, bounded ring buffers wherever possible.
* All code must compile with zero errors and zero warnings under `-Wall -Wextra -Werror`.
* Your changes must pass a clean bill of health via `valgrind --leak-check=full` with zero leaks and zero memory errors.

---

## Development & Testing Workflow

### 1. Setup Your Local Environment
Fork the repository, clone it locally, and ensure you can successfully build the test harness:

```bash
make clean
make test
LD_LIBRARY_PATH=test/deps ./test/test_runner
```

### 2. Regression and Stress Testing
If you modify the core scheduling or task dispatch engines, you must update the test parameters to execute a massive parallel load test (e.g., dispatching 50,000+ asynchronous tasks). Ensure the system completely clears down infrastructure tracks without crashing, dropping data streams, or hanging up threads.

### 3. Profiling for False Sharing
If you alter the tracking structures or arrays inside struct `cortlet_sched_t` or `cortlet_worker_t`, verify that you have not broken cache line isolation. Structures must preserve their explicit 64-byte boundary markings `(_Alignas(64))` to completely avoid false sharing performance drops across multicore systems.

Submission Checklist
Before opening a Pull Request, confirm that you can check off every step:

[  ] Code builds flawlessly on standard Linux distributions (gcc or clang).

[  ] No regression drops occur under heavy, multi-thousand concurrent task workloads.

[  ] Running Valgrind confirms a clean heap trace with zero leaked allocations.

[  ] Commit history is clean, logically split, and descriptive.

[  ] Code style matches the existing codebase (explicit variable typing, clean spacing, detailed comment notation on atomic barriers).

# Need Help?
If you are stuck mapping out a complex atomic race or tracing an affinity bug across asymmetric CPU layouts, don't guess. Open an architectural issue describing your approach, or reach out to the engineering core at compliance@cortlet.com.
