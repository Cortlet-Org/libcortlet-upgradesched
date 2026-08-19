use std::sync::atomic::{
    AtomicU64,
    Ordering,
};

#[derive(Debug)]
pub(crate) struct SchedulerStats {
    submitted: AtomicU64,
    completed: AtomicU64,
    rejected_full: AtomicU64,
    steals_attempted: AtomicU64,
    steals_succeeded: AtomicU64,
    fairness_yields: AtomicU64,
    steal_failures: AtomicU64,
}

#[derive(
    Debug,
    Clone,
    Copy,
    Default,
    PartialEq,
    Eq,
)]
pub(crate) struct SchedulerStatsSnapshot {
    pub(crate) submitted: u64,
    pub(crate) completed: u64,
    pub(crate) rejected_full: u64,
    pub(crate) steals_attempted: u64,
    pub(crate) steals_succeeded: u64,
    pub(crate) fairness_yields: u64,
    pub(crate) steal_failures: u64,
}

impl SchedulerStats {
    #[must_use]
    pub(crate) fn new() -> Self {
        Self {
            submitted: AtomicU64::new(0),
            completed: AtomicU64::new(0),
            rejected_full: AtomicU64::new(0),
            steals_attempted: AtomicU64::new(0),
            steals_succeeded: AtomicU64::new(0),
            fairness_yields: AtomicU64::new(0),
            steal_failures: AtomicU64::new(0),
        }
    }

    pub(crate) fn record_submission(
        &self,
    ) {
        self.submitted.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_completion(
        &self,
    ) {
        self.completed.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_queue_full(
        &self,
    ) {
        self.rejected_full.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_steal_attempt(
        &self,
    ) {
        self.steals_attempted.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_steal_success(
        &self,
    ) {
        self.steals_succeeded.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_fairness_yield(
        &self,
    ) {
        self.fairness_yields.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    pub(crate) fn record_steal_failure(
        &self,
    ) {
        self.steal_failures.fetch_add(
            1,
            Ordering::Relaxed,
        );
    }

    #[must_use]
    pub(crate) fn snapshot(
        &self,
    ) -> SchedulerStatsSnapshot {
        SchedulerStatsSnapshot {
            submitted:
            self.submitted.load(
                Ordering::Relaxed,
            ),

            completed:
            self.completed.load(
                Ordering::Relaxed,
            ),

            rejected_full:
            self.rejected_full.load(
                Ordering::Relaxed,
            ),

            steals_attempted:
            self.steals_attempted.load(
                Ordering::Relaxed,
            ),

            steals_succeeded:
            self.steals_succeeded.load(
                Ordering::Relaxed,
            ),

            fairness_yields:
            self.fairness_yields.load(
                Ordering::Relaxed,
            ),

            steal_failures:
            self.steal_failures.load(
                Ordering::Relaxed,
            ),
        }
    }
}

impl Default for SchedulerStats {
    fn default() -> Self {
        Self::new()
    }
}