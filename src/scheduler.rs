use std::error::Error;
use std::ffi::c_void;
use std::fmt;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::thread::{
    self,
    JoinHandle,
};

use crate::group::TaskGroup;
use crate::queue::{
    QueueSystem,
    Task,
    TaskFunction,
    TaskPriority,
};
use crate::topology::{
    pin_current_thread,
    Topology,
};
use crate::worker::{
    worker_main,
    WorkerShared,
};

use crate::stats::SchedulerStatsSnapshot;
use crate::queue::HIGH_BURST_LIMIT;

/// Errors produced by the scheduler core.
#[derive(Debug)]
pub(crate) enum SchedulerError {
    /// A worker thread could not be created.
    WorkerCreationFailed,

    /// The scheduler has started shutting down and no longer accepts work.
    SchedulerStopped,

    /// The configured scheduler queue capacity has been reached.
    QueueFull,

    /// Internal scheduler state became invalid.
    InternalError,
}

/// Internal scheduler configuration.
///
/// This is translated from the public C configuration structure
/// inside `ffi.rs`.
#[derive(Debug, Clone, Copy)]
pub(crate) struct SchedulerConfig {
    /// Number of worker threads.
    ///
    /// 0 means automatic detection.
    pub(crate) worker_count: usize,

    /// Whether workers should attempt CPU pinning.
    pub(crate) pin_workers: bool,

    /// Maximum number of outstanding tasks.
    ///
    /// 0 means unlimited.
    pub(crate) queue_capacity: usize,
}

impl SchedulerConfig {
    /// Returns the default scheduler configuration.
    ///
    /// Default behavior:
    ///
    /// - automatically detect worker count
    /// - enable CPU affinity
    /// - unlimited outstanding tasks
    #[must_use]
    pub(crate) fn automatic() -> Self {
        Self {
            worker_count: 0,
            pin_workers: true,
            queue_capacity: 0,
        }
    }
}

impl Default for SchedulerConfig {
    fn default() -> Self {
        Self::automatic()
    }
}

impl fmt::Display for SchedulerError {
    fn fmt(
        &self,
        formatter: &mut fmt::Formatter<'_>,
    ) -> fmt::Result {
        match self {
            Self::WorkerCreationFailed => {
                write!(
                    formatter,
                    "failed to create scheduler worker"
                )
            }

            Self::SchedulerStopped => {
                write!(
                    formatter,
                    "scheduler has stopped"
                )
            }

            Self::QueueFull => {
                write!(
                    formatter,
                    "scheduler queue is full"
                )
            }

            Self::InternalError => {
                write!(
                    formatter,
                    "internal scheduler error"
                )
            }
        }
    }
}

impl Error for SchedulerError {}

/// Main internal scheduler implementation.
///
/// The scheduler itself is never exposed directly to C.
///
/// `ffi.rs` owns the opaque C-compatible handle and forwards operations
/// into this structure.
pub(crate) struct Scheduler {
    /// State shared by all worker threads.
    shared: Arc<WorkerShared>,

    /// Handles for every scheduler worker.
    workers: Vec<JoinHandle<()>>,

    /// Number of worker threads owned by this scheduler.
    worker_count: usize,

    /// Detected CPU topology.
    topology: Topology,

    /// Whether CPU affinity was requested.
    pin_workers: bool,

    /// Maximum number of outstanding tasks.
    ///
    /// 0 means unlimited.
    queue_capacity: usize,
}

impl fmt::Debug for Scheduler {
    fn fmt(
        &self,
        formatter: &mut fmt::Formatter<'_>,
    ) -> fmt::Result {
        formatter
            .debug_struct("Scheduler")
            .field(
                "worker_count",
                &self.worker_count,
            )
            .field(
                "tasks_in_flight",
                &self.tasks_in_flight(),
            )
            .field(
                "shutdown",
                &self
                    .shared
                    .shutdown
                    .load(Ordering::Acquire),
            )
            .field(
                "cpu_count",
                &self.topology.cpu_count(),
            )
            .field(
                "pin_workers",
                &self.pin_workers,
            )
            .field(
                "queue_capacity",
                &self.queue_capacity,
            )
            .finish_non_exhaustive()
    }
}

impl Scheduler {
    /// Creates a scheduler using the default v2 configuration.
    ///
    /// Defaults:
    ///
    /// - automatic worker count
    /// - CPU affinity enabled
    /// - unlimited task capacity
    pub(crate) fn new(
    ) -> Result<Self, SchedulerError> {
        Self::from_config(
            SchedulerConfig::automatic(),
        )
    }

    /// Creates a scheduler using a custom configuration.
    pub(crate) fn from_config(
        config: SchedulerConfig,
    ) -> Result<Self, SchedulerError> {
        let topology =
            Topology::detect();

        let worker_count =
            if config.worker_count == 0 {
                topology
                    .cpu_count()
                    .max(1)
            } else {
                config.worker_count
            };

        Self::create(
            worker_count,
            topology,
            config.pin_workers,
            config.queue_capacity,
        )
    }

    /// Creates a scheduler with an explicit worker count.
    ///
    /// CPU affinity remains enabled.
    ///
    /// Queue capacity remains unlimited.
    pub(crate) fn with_worker_count(
        worker_count: usize,
    ) -> Result<Self, SchedulerError> {
        Self::from_config(
            SchedulerConfig {
                worker_count:
                worker_count.max(1),

                pin_workers:
                true,

                queue_capacity:
                0,
            },
        )
    }

    /// Internal scheduler constructor.
    fn create(
        worker_count: usize,
        topology: Topology,
        pin_workers: bool,
        queue_capacity: usize,
    ) -> Result<Self, SchedulerError> {
        let worker_count =
            worker_count.max(1);

        /*
         * QueueSystem::new() creates:
         *
         *   - global priority injectors
         *   - one local deque per worker
         *   - one Stealer for every worker
         */
        let (
            queues,
            local_queues,
        ) = QueueSystem::new(
            worker_count,
        );

        let shared =
            Arc::new(
                WorkerShared::new(
                    queues,
                ),
            );

        let mut workers =
            Vec::with_capacity(
                worker_count,
            );

        /*
         * Each local queue belongs to exactly one worker.
         *
         * crossbeam_deque::Worker is deliberately owned by one thread.
         */
        for (
            worker_id,
            local_queue,
        ) in local_queues
            .into_iter()
            .enumerate()
        {
            let worker_shared =
                Arc::clone(
                    &shared,
                );

            /*
             * Only calculate an assigned CPU if affinity was requested.
             */
            let assigned_cpu =
                if pin_workers {
                    topology
                        .cpu_for_worker(
                            worker_id,
                        )
                } else {
                    None
                };

            let result =
                thread::Builder::new()
                    .name(
                        format!(
                            "cortlet-worker-{worker_id}"
                        ),
                    )
                    .spawn(
                        move || {
                            /*
                             * CPU affinity is an optimization.
                             *
                             * Failure to pin does not prevent the worker
                             * from running.
                             */
                            if let Some(cpu) =
                                assigned_cpu
                            {
                                let _ =
                                    pin_current_thread(
                                        cpu,
                                    );
                            }

                            worker_main(
                                worker_id,
                                local_queue,
                                worker_shared,
                            );
                        },
                    );

            match result {
                Ok(handle) => {
                    workers.push(
                        handle,
                    );
                }

                Err(_) => {
                    /*
                     * Worker creation failed partway through startup.
                     */
                    shared
                        .begin_shutdown();

                    for handle in workers {
                        let _ =
                            handle.join();
                    }

                    return Err(
                        SchedulerError::
                        WorkerCreationFailed,
                    );
                }
            }
        }

        Ok(
            Self {
                shared,
                workers,
                worker_count,
                topology,
                pin_workers,
                queue_capacity,
            },
        )
    }

    /// Attempts to reserve one scheduler task slot.
    ///
    /// When `queue_capacity == 0`, capacity is unlimited.
    ///
    /// For bounded schedulers, this uses an atomic compare/exchange loop so
    /// multiple producer threads cannot race past the configured limit.
    fn reserve_task_slot(
        &self,
    ) -> Result<(), SchedulerError> {
        if self.queue_capacity == 0 {
            self.shared
                .tasks_in_flight
                .fetch_add(
                    1,
                    Ordering::AcqRel,
                );

            return Ok(());
        }

        let mut current =
            self.shared
                .tasks_in_flight
                .load(Ordering::Acquire);

        loop {
            if current >= self.queue_capacity {
                self.shared
                    .stats
                    .record_queue_full();

                return Err(
                    SchedulerError::QueueFull,
                );
            }

            match self
                .shared
                .tasks_in_flight
                .compare_exchange_weak(
                    current,
                    current + 1,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                )
            {
                Ok(_) => {
                    return Ok(());
                }

                Err(actual) => {
                    current = actual;
                }
            }
        }
    }

    /* -----------------------------------------------------
     * Scheduler information
     * ----------------------------------------------------- */

    /// Returns the number of scheduler workers.
    #[must_use]
    pub(crate) fn worker_count(
        &self,
    ) -> usize {
        self.worker_count
    }

    /// Returns whether worker CPU pinning was requested.
    #[must_use]
    pub(crate) fn pin_workers(
        &self,
    ) -> bool {
        self.pin_workers
    }

    /// Returns the configured maximum number of outstanding tasks.
    ///
    /// 0 means unlimited.
    #[must_use]
    pub(crate) fn queue_capacity(
        &self,
    ) -> usize {
        self.queue_capacity
    }

    /// Returns the number of tasks that have been submitted but have not
    /// completed yet.
    #[must_use]
    pub(crate) fn tasks_in_flight(
        &self,
    ) -> usize {
        self.shared
            .tasks_in_flight
            .load(
                Ordering::Acquire,
            )
    }

    /// Returns whether scheduler shutdown has begun.
    #[must_use]
    pub(crate) fn is_stopped(
        &self,
    ) -> bool {
        self.shared
            .shutdown
            .load(
                Ordering::Acquire,
            )
    }

    #[must_use]
    pub(crate) fn stats(
        &self,
    ) -> SchedulerStatsSnapshot {
        self.shared
            .stats
            .snapshot()
    }

    #[must_use]
    pub(crate) fn outstanding_tasks(
        &self,
    ) -> usize {
        self.shared
            .tasks_in_flight
            .load(Ordering::Acquire)
    }

    #[must_use]
    pub(crate) fn sleeping_workers(
        &self,
    ) -> usize {
        self.shared
            .sleeping_workers
            .load(Ordering::Acquire)
    }

    #[must_use]
    pub(crate) fn high_burst_limit() -> usize {
        HIGH_BURST_LIMIT
    }

    /* -----------------------------------------------------
     * Task submission
     * ----------------------------------------------------- */

    /// Submits a NORMAL-priority task.
    pub(crate) fn push(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            TaskPriority::Normal,
            None,
        )
    }

    /// Submits a task with an explicit priority.
    pub(crate) fn push_priority(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            priority,
            None,
        )
    }

    /// Submits a NORMAL-priority task belonging to a task group.
    pub(crate) fn push_group(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        group: Arc<TaskGroup>,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            TaskPriority::Normal,
            Some(group),
        )
    }

    /// Submits a grouped task with an explicit priority.
    pub(crate) fn push_group_priority(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
        group: Arc<TaskGroup>,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            priority,
            Some(group),
        )
    }

    /// Common implementation for every task submission path.
    ///
    /// Capacity enforcement will be added here next.
    fn push_internal(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
        group: Option<Arc<TaskGroup>>,
    ) -> Result<(), SchedulerError> {
        /*
         * Reject submissions immediately once shutdown begins.
         */
        if self
            .shared
            .shutdown
            .load(Ordering::Acquire)
        {
            return Err(
                SchedulerError::
                SchedulerStopped,
            );
        }

        /*
         * Reserve capacity before making any group accounting changes.
         *
         * This is atomic, so concurrent producer threads cannot all observe
         * the same free slot and exceed the configured capacity.
         */
        self.reserve_task_slot()?;

        /*
         * Shutdown may have started immediately after we reserved the slot.
         *
         * Roll the reservation back if that happened.
         */
        if self
            .shared
            .shutdown
            .load(Ordering::Acquire)
        {
            self.release_task_slot();

            return Err(
                SchedulerError::
                SchedulerStopped,
            );
        }

        /*
         * Register with the optional task group.
         *
         * If the group is closed, release the scheduler slot because no
         * task will actually be published.
         */
        if let Some(
            task_group,
        ) = group.as_ref()
        {
            if !task_group
                .add_task()
            {
                self.release_task_slot();

                return Err(
                    SchedulerError::
                    SchedulerStopped,
                );
            }
        }

        /*
         * Final shutdown check.
         *
         * At this point both:
         *
         * - scheduler accounting
         * - optional group accounting
         *
         * have been reserved, so both must be rolled back on failure.
         */
        if self
            .shared
            .shutdown
            .load(Ordering::Acquire)
        {
            self.release_task_slot();

            if let Some(
                task_group,
            ) = group.as_ref()
            {
                task_group
                    .task_completed();
            }

            return Err(
                SchedulerError::
                SchedulerStopped,
            );
        }

        /*
         * Accounting has succeeded.
         *
         * The task can now safely become visible to workers.
         */
        let task =
            Task::new(
                function,
                argument,
                priority,
                group,
            );

        self.shared
            .queues
            .push_global(
                task,
            );

        /*
         * The task has now been successfully accepted and published.
         */
        self.shared
            .stats
            .record_submission();

        self.shared
            .wake_one();
        Ok(())
    }

    pub(crate) fn try_push(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            TaskPriority::Normal,
            None,
        )
    }

    pub(crate) fn try_push_priority(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            priority,
            None,
        )
    }

    pub(crate) fn try_push_group(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        group: Arc<TaskGroup>,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            TaskPriority::Normal,
            Some(group),
        )
    }

    pub(crate) fn try_push_group_priority(
        &self,
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
        group: Arc<TaskGroup>,
    ) -> Result<(), SchedulerError> {
        self.push_internal(
            function,
            argument,
            priority,
            Some(group),
        )
    }

    /// Releases a task slot that was reserved but never published.
    fn release_task_slot(
        &self,
    ) {
        self.shared
            .tasks_in_flight
            .fetch_sub(
                1,
                Ordering::AcqRel,
            );
    }

    /* -----------------------------------------------------
     * Synchronization
     * ----------------------------------------------------- */

    /// Blocks until all outstanding scheduler tasks have completed.
    pub(crate) fn wait(
        &self,
    ) -> Result<(), SchedulerError> {
        let mut guard =
            self.shared
                .sleep_lock
                .lock();

        while self
            .shared
            .tasks_in_flight
            .load(
                Ordering::Acquire,
            )
            != 0
        {
            self.shared
                .wake
                .wait(
                    &mut guard,
                );
        }

        Ok(())
    }

    /* -----------------------------------------------------
     * Shutdown
     * ----------------------------------------------------- */

    /// Begins graceful scheduler shutdown.
    pub(crate) fn shutdown(
        &self,
    ) {
        self.shared
            .begin_shutdown();
    }

    /// Joins every scheduler worker.
    fn join_workers(
        &mut self,
    ) {
        while let Some(worker) =
            self.workers.pop()
        {
            /*
             * Worker panics must never propagate through the C ABI.
             */
            let _ =
                worker.join();
        }
    }
}

impl Drop for Scheduler {
    fn drop(
        &mut self,
    ) {
        /*
         * Graceful destruction:
         *
         *          reject new tasks
         *                 │
         *                 ▼
         *          shutdown = true
         *                 │
         *                 ▼
         *        wake sleeping workers
         *                 │
         *                 ▼
         *         drain pending work
         *                 │
         *                 ▼
         *        tasks_in_flight == 0
         *                 │
         *                 ▼
         *          workers terminate
         *                 │
         *                 ▼
         *            join threads
         */

        self.shutdown();

        self.join_workers();
    }
}