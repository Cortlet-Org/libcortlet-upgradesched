use std::sync::atomic::{
    AtomicBool,
    AtomicUsize,
    Ordering,
};

use parking_lot::{
    Condvar,
    Mutex,
};

#[derive(Debug)]
pub(crate) struct TaskGroup {
    /// Number of tasks that belong to this group and have not completed yet.
    tasks_in_flight: AtomicUsize,

    /// Prevents new tasks from being attached after destruction/shutdown logic starts.
    closed: AtomicBool,

    /// Synchronizes waiting.
    wait_lock: Mutex<()>,

    /// Wakes threads waiting for this group's tasks to finish.
    wake: Condvar,
}

impl TaskGroup {
    #[must_use]
    pub(crate) fn new() -> Self {
        Self {
            tasks_in_flight: AtomicUsize::new(0),
            closed: AtomicBool::new(false),
            wait_lock: Mutex::new(()),
            wake: Condvar::new(),
        }
    }

    /// Registers a task with this group.
    pub(crate) fn add_task(
        &self,
    ) -> bool {
        if self
            .closed
            .load(Ordering::Acquire)
        {
            return false;
        }

        self.tasks_in_flight
            .fetch_add(
                1,
                Ordering::AcqRel,
            );

        /*
         * Re-check after incrementing so closing the group cannot silently
         * race with registration.
         */
        if self
            .closed
            .load(Ordering::Acquire)
        {
            self.task_completed();

            return false;
        }

        true
    }

    /// Marks one group task as complete.
    pub(crate) fn task_completed(
        &self,
    ) {
        let previous =
            self.tasks_in_flight
                .fetch_sub(
                    1,
                    Ordering::AcqRel,
                );

        if previous == 1 {
            self.wake.notify_all();
        }
    }

    /// Waits until this group's task count reaches zero.
    pub(crate) fn wait(
        &self,
    ) {
        let mut guard =
            self.wait_lock.lock();

        while self
            .tasks_in_flight
            .load(Ordering::Acquire)
            != 0
        {
            self.wake.wait(
                &mut guard,
            );
        }
    }

    /// Prevents future task registration.
    pub(crate) fn close(
        &self,
    ) {
        self.closed.store(
            true,
            Ordering::Release,
        );

        self.wake.notify_all();
    }

    #[must_use]
    pub(crate) fn tasks_in_flight(
        &self,
    ) -> usize {
        self.tasks_in_flight
            .load(Ordering::Acquire)
    }

    #[must_use]
    pub(crate) fn is_closed(
        &self,
    ) -> bool {
        self.closed
            .load(Ordering::Acquire)
    }
}

impl Default for TaskGroup {
    fn default() -> Self {
        Self::new()
    }
}