use std::sync::atomic::{
    AtomicBool,
    AtomicUsize,
    Ordering,
};
use std::sync::Arc;

use crossbeam_deque::Worker;
use parking_lot::{
    Condvar,
    Mutex,
};

use crate::queue::{
    PopResult,
    PriorityState,
    QueueSystem,
    Task,
};
use crate::stats::SchedulerStats;
use crate::steal::{
    StealPolicy,
    StealState,
};

/// Shared state used by all scheduler workers.
///
/// This is created once by `scheduler.rs` and wrapped in `Arc`.
#[derive(Debug)]
pub(crate) struct WorkerShared {
    /// Global priority queues + peer stealers.
    pub(crate) queues: QueueSystem,

    /// Number of scheduler-wide tasks that have not completed yet.
    pub(crate) tasks_in_flight: AtomicUsize,

    /// Number of workers preparing to sleep or currently sleeping.
    pub(crate) sleeping_workers: AtomicUsize,

    /// Set once graceful scheduler shutdown begins.
    pub(crate) shutdown: AtomicBool,

    /// Synchronizes worker sleeping and scheduler waiting.
    pub(crate) sleep_lock: Mutex<()>,

    /// Used for:
    ///
    /// - waking workers when new work arrives
    /// - waking `cortlet_sched_wait()`
    /// - waking workers during shutdown
    pub(crate) wake: Condvar,

    /// Shared scheduler statistics.
    pub(crate) stats: SchedulerStats,
}

impl WorkerShared {
    #[must_use]
    pub(crate) fn new(
        queues: QueueSystem,
    ) -> Self {
        Self {
            queues,

            tasks_in_flight:
            AtomicUsize::new(0),

            sleeping_workers:
            AtomicUsize::new(0),

            shutdown:
            AtomicBool::new(false),

            sleep_lock:
            Mutex::new(()),

            wake:
            Condvar::new(),

            stats:
            SchedulerStats::new(),
        }
    }

    /// Marks one scheduler-wide task as completed.
    pub(crate) fn task_completed(
        &self,
    ) {
        /*
         * Record the completion before reducing the outstanding count.
         */
        self.stats
            .record_completion();

        let previous =
            self.tasks_in_flight
                .fetch_sub(
                    1,
                    Ordering::AcqRel,
                );

        /*
         * 1 -> 0 means the scheduler has become idle.
         *
         * Wake all threads waiting inside cortlet_sched_wait().
         */
        if previous == 1 {
            self.wake.notify_all();
        }
    }

    /// Returns whether this worker should terminate.
    ///
    /// Workers only terminate when:
    ///
    /// - shutdown has been requested
    /// - every outstanding task has completed
    #[must_use]
    pub(crate) fn should_stop(
        &self,
    ) -> bool {
        self.shutdown
            .load(
                Ordering::Acquire,
            )
            && self
            .tasks_in_flight
            .load(
                Ordering::Acquire,
            )
            == 0
    }

    /// Starts graceful shutdown.
    pub(crate) fn begin_shutdown(
        &self,
    ) {
        self.shutdown.store(
            true,
            Ordering::Release,
        );

        /*
         * Wake all sleeping workers so they can either drain work
         * or observe that shutdown is complete.
         */
        self.wake.notify_all();
    }

    /// Wakes one worker only if at least one may currently be sleeping.
    pub(crate) fn wake_one(
        &self,
    ) {
        if self
            .sleeping_workers
            .load(
                Ordering::Acquire,
            )
            != 0
        {
            self.wake.notify_one();
        }
    }

    /// Wakes every sleeping worker.
    pub(crate) fn wake_all(
        &self,
    ) {
        self.wake.notify_all();
    }
}

/// Executes a task and performs all completion accounting.
///
/// Completion order:
///
/// 1. execute callback
/// 2. increment scheduler completed statistics
/// 3. decrement scheduler-wide task count
/// 4. decrement task-group count, if present
fn execute_task(
    shared: &WorkerShared,
    mut task: Task,
) {
    /*
     * Remove the group reference before execution.
     *
     * The Arc remains alive locally until the task is completely
     * accounted for.
     */
    let group =
        task.take_group();

    unsafe {
        task.execute();
    }

    /*
     * Mark the task complete globally.
     *
     * This also updates the scheduler's completed-task statistics.
     */
    shared.task_completed();

    /*
     * Then mark it complete within its optional group.
     */
    if let Some(group) = group {
        group.task_completed();
    }
}

/// Main scheduler worker loop.
///
/// Each worker exclusively owns one local
/// `crossbeam_deque::Worker<Task>`.
///
/// Work lookup order:
///
/// 1. local deque
/// 2. fairness-aware global priority selection
/// 3. peer worker stealing
/// 4. sleep
pub(crate) fn worker_main(
    worker_id: usize,
    local_queue: Worker<Task>,
    shared: Arc<WorkerShared>,
) {
    let steal_state =
        StealState::new(
            worker_id,
            shared
                .queues
                .worker_count(),
            StealPolicy::RoundRobin,
        );

    /*
     * Priority fairness state is local to this worker.
     *
     * Each worker independently tracks how many HIGH-priority tasks
     * it has executed consecutively.
     */
    let mut priority_state =
        PriorityState::new();

    loop {
        /*
         * Fast path.
         *
         * Attempt to find work without touching the sleep mutex.
         */
        match shared
            .queues
            .pop_for_worker(
                &steal_state,
                &local_queue,
                &shared.stats,
                &mut priority_state,
            )
        {
            PopResult::Task(task) => {
                execute_task(
                    &shared,
                    task,
                );

                continue;
            }

            PopResult::Retry => {
                continue;
            }

            PopResult::Empty => {}
        }

        if shared.should_stop() {
            break;
        }

        /*
         * Slow path.
         *
         * Take the sleep mutex before preparing to block.
         */
        let mut guard =
            shared
                .sleep_lock
                .lock();

        /*
         * Re-check for work after obtaining the lock.
         */
        match shared
            .queues
            .pop_for_worker(
                &steal_state,
                &local_queue,
                &shared.stats,
                &mut priority_state,
            )
        {
            PopResult::Task(task) => {
                drop(
                    guard,
                );

                execute_task(
                    &shared,
                    task,
                );

                continue;
            }

            PopResult::Retry => {
                drop(
                    guard,
                );

                continue;
            }

            PopResult::Empty => {}
        }

        if shared.should_stop() {
            break;
        }

        /*
         * Publish our sleeping state.
         *
         * Submission uses this counter to avoid unnecessary notify_one()
         * calls when all workers are already active.
         */
        shared
            .sleeping_workers
            .fetch_add(
                1,
                Ordering::AcqRel,
            );

        /*
         * One final work check after publishing ourselves as sleeping.
         *
         * This closes the race between:
         *
         * - task submission
         * - the worker actually entering Condvar::wait()
         */
        match shared
            .queues
            .pop_for_worker(
                &steal_state,
                &local_queue,
                &shared.stats,
                &mut priority_state,
            )
        {
            PopResult::Task(task) => {
                shared
                    .sleeping_workers
                    .fetch_sub(
                        1,
                        Ordering::AcqRel,
                    );

                drop(
                    guard,
                );

                execute_task(
                    &shared,
                    task,
                );

                continue;
            }

            PopResult::Retry => {
                shared
                    .sleeping_workers
                    .fetch_sub(
                        1,
                        Ordering::AcqRel,
                    );

                drop(
                    guard,
                );

                continue;
            }

            PopResult::Empty => {}
        }

        if shared.should_stop() {
            shared
                .sleeping_workers
                .fetch_sub(
                    1,
                    Ordering::AcqRel,
                );

            break;
        }

        /*
         * No work exists, so sleep until:
         *
         * - new work arrives
         * - shutdown begins
         * - another scheduler event wakes workers
         */
        shared
            .wake
            .wait(
                &mut guard,
            );

        shared
            .sleeping_workers
            .fetch_sub(
                1,
                Ordering::AcqRel,
            );
    }
}