use std::ffi::c_void;
use std::sync::Arc;

use crossbeam_deque::{
    Injector,
    Steal,
    Stealer,
    Worker,
};

use crate::group::TaskGroup;
use crate::stats::SchedulerStats;
use crate::steal::StealState;

pub(crate) type TaskFunction =
unsafe extern "C" fn(*mut c_void);

pub(crate) const HIGH_BURST_LIMIT: usize = 32;

#[derive(Debug)]
pub(crate) struct PriorityState {
    high_streak: usize,
}

impl PriorityState {
    #[must_use]
    pub(crate) fn new() -> Self {
        Self {
            high_streak: 0,
        }
    }

    fn record_high(
        &mut self,
    ) {
        self.high_streak += 1;
    }

    fn reset(
        &mut self,
    ) {
        self.high_streak = 0;
    }

    #[must_use]
    fn fairness_due(
        &self,
    ) -> bool {
        self.high_streak >= HIGH_BURST_LIMIT
    }

    fn record_task(
        &mut self,
        task: &Task,
    ) {
        match task.priority() {
            TaskPriority::High => {
                self.record_high();
            }

            TaskPriority::Normal
            | TaskPriority::Low => {
                self.reset();
            }
        }
    }
}

impl Default for PriorityState {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(
    Debug,
    Clone,
    Copy,
    PartialEq,
    Eq,
)]
pub(crate) enum TaskPriority {
    Low,
    Normal,
    High,
}

#[derive(Debug, Clone)]
pub(crate) struct Task {
    function: TaskFunction,
    argument: usize,
    priority: TaskPriority,
    group: Option<Arc<TaskGroup>>,
}

impl Task {
    #[must_use]
    pub(crate) fn new(
        function: TaskFunction,
        argument: *mut c_void,
        priority: TaskPriority,
        group: Option<Arc<TaskGroup>>,
    ) -> Self {
        Self {
            function,
            argument: argument as usize,
            priority,
            group,
        }
    }

    #[must_use]
    pub(crate) fn priority(
        &self,
    ) -> TaskPriority {
        self.priority
    }

    pub(crate) fn take_group(
        &mut self,
    ) -> Option<Arc<TaskGroup>> {
        self.group.take()
    }

    pub(crate) unsafe fn execute(
        &self,
    ) {
        let argument =
            self.argument as *mut c_void;

        unsafe {
            (self.function)(argument);
        }
    }
}

#[derive(Debug)]
pub(crate) enum PopResult {
    Task(Task),
    Empty,
    Retry,
}

#[derive(Debug)]
pub(crate) struct QueueSystem {
    high: Arc<Injector<Task>>,
    normal: Arc<Injector<Task>>,
    low: Arc<Injector<Task>>,

    stealers: Arc<Vec<Stealer<Task>>>,
}

impl QueueSystem {
    #[must_use]
    pub(crate) fn new(
        worker_count: usize,
    ) -> (
        Self,
        Vec<Worker<Task>>,
    ) {
        let worker_count =
            worker_count.max(1);

        let mut local_workers =
            Vec::with_capacity(
                worker_count,
            );

        let mut stealers =
            Vec::with_capacity(
                worker_count,
            );

        for _ in 0..worker_count {
            let worker =
                Worker::new_fifo();

            stealers.push(
                worker.stealer(),
            );

            local_workers.push(
                worker,
            );
        }

        (
            Self {
                high: Arc::new(
                    Injector::new(),
                ),

                normal: Arc::new(
                    Injector::new(),
                ),

                low: Arc::new(
                    Injector::new(),
                ),

                stealers: Arc::new(
                    stealers,
                ),
            },

            local_workers,
        )
    }

    pub(crate) fn push_global(
        &self,
        task: Task,
    ) {
        match task.priority() {
            TaskPriority::High => {
                self.high.push(
                    task,
                );
            }

            TaskPriority::Normal => {
                self.normal.push(
                    task,
                );
            }

            TaskPriority::Low => {
                self.low.push(
                    task,
                );
            }
        }
    }

    #[must_use]
    pub(crate) fn worker_count(
        &self,
    ) -> usize {
        self.stealers.len()
    }

    pub(crate) fn pop_for_worker(
        &self,
        steal_state: &StealState,
        local: &Worker<Task>,
        stats: &SchedulerStats,
        priority_state: &mut PriorityState,
    ) -> PopResult {
        /*
         * Fairness must be checked BEFORE consuming another local task.
         *
         * A global HIGH steal may have filled the local deque with a batch
         * of HIGH tasks. If we always consume local work first, lower
         * priorities can remain starved even after HIGH_BURST_LIMIT.
         */
        if priority_state.fairness_due() {
            /*
             * Record that this worker deliberately entered the
             * lower-priority fairness path.
             */
            stats.record_fairness_yield();

            if let Some(result) =
                self.try_priority_queue(
                    local,
                    &self.normal,
                    priority_state,
                )
            {
                return result;
            }

            if let Some(result) =
                self.try_priority_queue(
                    local,
                    &self.low,
                    priority_state,
                )
            {
                return result;
            }

            /*
             * Nothing lower-priority was available.
             *
             * Reset the streak and continue normal execution.
             */
            priority_state.reset();
        }

        /*
         * Local work remains the normal fast path when fairness
         * intervention is not required.
         */
        if let Some(task) =
            local.pop()
        {
            priority_state.record_task(
                &task,
            );

            return PopResult::Task(
                task,
            );
        }

        /*
         * Normal global priority order:
         *
         * HIGH -> NORMAL -> LOW
         */
        if let Some(result) =
            self.try_priority_queue(
                local,
                &self.high,
                priority_state,
            )
        {
            return result;
        }

        if let Some(result) =
            self.try_priority_queue(
                local,
                &self.normal,
                priority_state,
            )
        {
            return result;
        }

        if let Some(result) =
            self.try_priority_queue(
                local,
                &self.low,
                priority_state,
            )
        {
            return result;
        }

        /*
         * No global or local work exists.
         *
         * Try stealing from another worker.
         */
        self.steal_from_peers(
            steal_state,
            stats,
            priority_state,
        )
    }

    /*
     * Tries one priority injector.
     *
     * Returns:
     *
     * None
     *     queue was empty, continue checking other queues
     *
     * Some(PopResult::Task)
     *     task obtained
     *
     * Some(PopResult::Retry)
     *     crossbeam requested a retry
     */
    fn try_priority_queue(
        &self,
        local: &Worker<Task>,
        injector: &Injector<Task>,
        priority_state: &mut PriorityState,
    ) -> Option<PopResult> {
        match self.steal_priority(
            local,
            injector,
        ) {
            PopResult::Task(task) => {
                priority_state.record_task(
                    &task,
                );

                Some(
                    PopResult::Task(
                        task,
                    ),
                )
            }

            PopResult::Retry => {
                Some(
                    PopResult::Retry,
                )
            }

            PopResult::Empty => {
                None
            }
        }
    }

    fn steal_priority(
        &self,
        local: &Worker<Task>,
        injector: &Injector<Task>,
    ) -> PopResult {
        match injector
            .steal_batch_and_pop(
                local,
            )
        {
            Steal::Success(task) => {
                PopResult::Task(
                    task,
                )
            }

            Steal::Retry => {
                PopResult::Retry
            }

            Steal::Empty => {
                PopResult::Empty
            }
        }
    }

    fn steal_from_peers(
        &self,
        steal_state: &StealState,
        stats: &SchedulerStats,
        priority_state: &mut PriorityState,
    ) -> PopResult {
        let victims =
            steal_state.victims();

        for victim in victims {
            let Some(stealer) =
                self.stealers.get(
                    victim,
                )
            else {
                continue;
            };

            stats.record_steal_attempt();

            match stealer.steal() {
                Steal::Success(task) => {
                    stats.record_steal_success();

                    priority_state.record_task(
                        &task,
                    );

                    return PopResult::Task(
                        task,
                    );
                }

                Steal::Retry => {
                    return PopResult::Retry;
                }

                Steal::Empty => {
                    stats.record_steal_failure();
                }
            }
        }

        PopResult::Empty
    }
}