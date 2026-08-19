use std::sync::atomic::{
    AtomicUsize,
    Ordering,
};

/// Strategy used to select a victim worker for work stealing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum StealPolicy {
    /// Probe workers in deterministic round-robin order.
    RoundRobin,

    /// Reserved for a future adaptive policy.
    Adaptive,
}

/// Per-worker steal state.
///
/// Each worker gets its own instance so victim selection can evolve
/// independently without putting extra contention on global state.
#[derive(Debug)]
pub(crate) struct StealState {
    worker_id: usize,
    worker_count: usize,
    policy: StealPolicy,
    cursor: AtomicUsize,
}

impl StealState {
    #[must_use]
    pub(crate) fn new(
        worker_id: usize,
        worker_count: usize,
        policy: StealPolicy,
    ) -> Self {
        Self {
            worker_id,
            worker_count: worker_count.max(1),
            policy,
            cursor: AtomicUsize::new(0),
        }
    }

    #[must_use]
    pub(crate) fn worker_id(&self) -> usize {
        self.worker_id
    }

    #[must_use]
    pub(crate) fn worker_count(&self) -> usize {
        self.worker_count
    }

    #[must_use]
    pub(crate) fn policy(&self) -> StealPolicy {
        self.policy
    }

    /// Produces an ordered list of victim worker IDs.
    ///
    /// The current worker is never included.
    #[must_use]
    pub(crate) fn victims(&self) -> Vec<usize> {
        match self.policy {
            StealPolicy::RoundRobin => {
                self.round_robin_victims()
            }

            StealPolicy::Adaptive => {
                /*
                 * Adaptive stealing is not implemented yet.
                 *
                 * For now, it intentionally falls back to round-robin
                 * instead of changing scheduler behavior unexpectedly.
                 */
                self.round_robin_victims()
            }
        }
    }

    fn round_robin_victims(&self) -> Vec<usize> {
        if self.worker_count <= 1 {
            return Vec::new();
        }

        let mut victims =
            Vec::with_capacity(self.worker_count - 1);

        /*
         * Rotate the initial victim on every steal attempt.
         *
         * This prevents one worker from always being selected first.
         */
        let rotation = self
            .cursor
            .fetch_add(1, Ordering::Relaxed);

        let first =
            (self.worker_id + 1 + rotation)
                % self.worker_count;

        for offset in 0..self.worker_count {
            let victim =
                (first + offset)
                    % self.worker_count;

            if victim == self.worker_id {
                continue;
            }

            victims.push(victim);

            if victims.len()
                == self.worker_count - 1
            {
                break;
            }
        }

        victims
    }
}