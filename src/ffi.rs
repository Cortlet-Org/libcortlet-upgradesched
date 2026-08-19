use std::ffi::c_void;
use std::ptr;
use std::sync::Arc;

use crate::group::TaskGroup;
use crate::queue::TaskPriority;
use crate::scheduler::{
    Scheduler,
    SchedulerConfig,
    SchedulerError,
};

/// Opaque scheduler handle exposed through the C ABI.
///
/// C code must only interact with this through pointers.
#[repr(C)]
#[derive(Debug)]
pub struct CortletScheduler {
    scheduler: Scheduler,
}

/// Opaque task-group handle exposed through the C ABI.
///
/// Internally this owns one `Arc<TaskGroup>`.
///
/// Scheduled tasks clone this Arc so destroying the public C handle
/// does not invalidate tasks that are already queued or running.
#[repr(C)]
#[derive(Debug)]
pub struct CortletTaskGroup {
    group: Arc<TaskGroup>,
}

/// C-compatible scheduler configuration.
///
/// This must stay layout-compatible with:
///
/// `cortlet_sched_config_t` in `include/cortlet-upgradesched.h`.
#[repr(C)]
#[derive(
    Debug,
    Clone,
    Copy,
)]
pub struct CortletSchedulerConfig {
    /// Number of workers.
    ///
    /// 0 means automatic detection.
    pub worker_count: usize,

    /// Non-zero enables worker CPU pinning.
    pub pin_workers: i32,

    /// Maximum number of outstanding tasks.
    ///
    /// 0 means unlimited.
    pub queue_capacity: usize,
}

/// C-compatible task function.
///
/// Equivalent C declaration:
///
/// `typedef void (*cortlet_task_fn)(void *argument);`
pub type CortletTaskFn =
unsafe extern "C" fn(*mut c_void);

/// Stable result codes exposed through the C ABI.
///
/// These values must match the public C header exactly.
#[repr(C)]
#[derive(
    Debug,
    Clone,
    Copy,
    PartialEq,
    Eq,
)]
pub enum CortletResult {
    Ok = 0,
    InvalidArgument = 1,
    AllocationFailed = 2,
    SchedulerStopped = 3,
    QueueFull = 4,
    InternalError = 255,
}

#[repr(C)]
#[derive(
    Debug,
    Clone,
    Copy,
    Default,
)]
pub struct CortletSchedulerStats {
    pub submitted: u64,
    pub completed: u64,
    pub rejected_full: u64,

    pub steals_attempted: u64,
    pub steals_succeeded: u64,
    pub steal_failures: u64,

    pub fairness_yields: u64,

    pub outstanding: usize,
}

/// Converts internal scheduler errors into stable public C error codes.
fn map_scheduler_error(
    error: SchedulerError,
) -> CortletResult {
    match error {
        SchedulerError::SchedulerStopped => {
            CortletResult::SchedulerStopped
        }

        SchedulerError::QueueFull => {
            CortletResult::QueueFull
        }

        SchedulerError::WorkerCreationFailed => {
            CortletResult::InternalError
        }

        SchedulerError::InternalError => {
            CortletResult::InternalError
        }
    }
}
/// Converts a raw C priority integer into the scheduler's internal priority.
///
/// Accepted values:
///
/// - 0 = LOW
/// - 1 = NORMAL
/// - 2 = HIGH
///
/// Any other value is rejected.
fn map_priority(
    priority: i32,
) -> Option<TaskPriority> {
    match priority {
        0 => {
            Some(
                TaskPriority::Low,
            )
        }

        1 => {
            Some(
                TaskPriority::Normal,
            )
        }

        2 => {
            Some(
                TaskPriority::High,
            )
        }

        _ => {
            None
        }
    }
}

/* ---------------------------------------------------------
 * Version
 * --------------------------------------------------------- */

#[unsafe(no_mangle)]
pub extern "C" fn cortlet_version_major(
) -> u32 {
    2
}

#[unsafe(no_mangle)]
pub extern "C" fn cortlet_version_minor(
) -> u32 {
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn cortlet_version_patch(
) -> u32 {
    0
}

/* ---------------------------------------------------------
 * Scheduler lifecycle
 * --------------------------------------------------------- */

/// Creates a scheduler using the default configuration.
///
/// Defaults:
///
/// - automatic worker count
/// - CPU affinity enabled
///
/// Returns NULL if initialization fails.
#[unsafe(no_mangle)]
pub extern "C" fn cortlet_sched_init(
) -> *mut CortletScheduler {
    let scheduler =
        match Scheduler::new() {
            Ok(scheduler) => {
                scheduler
            }

            Err(_) => {
                return ptr::null_mut();
            }
        };

    let handle =
        CortletScheduler {
            scheduler,
        };

    Box::into_raw(
        Box::new(
            handle,
        ),
    )
}

/// Creates a scheduler using an explicit configuration.
///
/// Passing NULL returns NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_init_ex(
    config: *const CortletSchedulerConfig,
) -> *mut CortletScheduler {
    if config.is_null() {
        return ptr::null_mut();
    }

    let config =
        unsafe {
            &*config
        };

    let scheduler_config =
        SchedulerConfig {
            worker_count:
            config.worker_count,

            pin_workers:
            config.pin_workers != 0,

            queue_capacity:
            config.queue_capacity,
        };

    let scheduler =
        match Scheduler::from_config(
            scheduler_config,
        ) {
            Ok(scheduler) => {
                scheduler
            }

            Err(_) => {
                return ptr::null_mut();
            }
        };

    let handle =
        CortletScheduler {
            scheduler,
        };

    Box::into_raw(
        Box::new(
            handle,
        ),
    )
}

/// Destroys a scheduler.
///
/// Passing NULL is allowed.
///
/// Existing work is drained through `Scheduler::drop()`.
///
/// This function must not run concurrently with any other operation
/// using the same scheduler pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_destroy(
    scheduler: *mut CortletScheduler,
) {
    if scheduler.is_null() {
        return;
    }

    unsafe {
        drop(
            Box::from_raw(
                scheduler,
            ),
        );
    }
}

/* ---------------------------------------------------------
 * Scheduler information
 * --------------------------------------------------------- */

/// Returns the scheduler's worker count.
///
/// Returns 0 when passed NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_worker_count(
    scheduler: *const CortletScheduler,
) -> usize {
    if scheduler.is_null() {
        return 0;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    scheduler
        .scheduler
        .worker_count()
}

/// Returns whether worker CPU pinning was requested.
///
/// Returns:
///
/// - 0 = disabled
/// - 1 = enabled
///
/// Passing NULL returns 0.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_pin_workers(
    scheduler: *const CortletScheduler,
) -> i32 {
    if scheduler.is_null() {
        return 0;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    if scheduler
        .scheduler
        .pin_workers()
    {
        1
    } else {
        0
    }
}

/* ---------------------------------------------------------
 * Task submission
 * --------------------------------------------------------- */

/// Submits a NORMAL-priority task for asynchronous execution.
///
/// Multiple C threads may submit concurrently as long as the scheduler
/// is not being destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_push(
    scheduler: *mut CortletScheduler,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    match scheduler
        .scheduler
        .push(
            task,
            argument,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

/// Submits a task using an explicit priority.
///
/// Priority values:
///
/// - 0 = LOW
/// - 1 = NORMAL
/// - 2 = HIGH
///
/// Any other value returns INVALID_ARGUMENT.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_push_priority(
    scheduler: *mut CortletScheduler,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
    priority: i32,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let Some(priority) =
        map_priority(
            priority,
        )
    else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    match scheduler
        .scheduler
        .push_priority(
            task,
            argument,
            priority,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

/* ---------------------------------------------------------
 * Task groups
 * --------------------------------------------------------- */

/// Creates a new task group.
///
/// Returns NULL if allocation fails.
#[unsafe(no_mangle)]
pub extern "C" fn cortlet_task_group_create(
) -> *mut CortletTaskGroup {
    let group =
        CortletTaskGroup {
            group:
            Arc::new(
                TaskGroup::new(),
            ),
        };

    Box::into_raw(
        Box::new(
            group,
        ),
    )
}

/// Submits a NORMAL-priority task belonging to a task group.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_push_group(
    scheduler: *mut CortletScheduler,
    group: *mut CortletTaskGroup,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
) -> CortletResult {
    if scheduler.is_null()
        || group.is_null()
    {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    let group =
        unsafe {
            &*group
        };

    let group_ref =
        Arc::clone(
            &group.group,
        );

    match scheduler
        .scheduler
        .push_group(
            task,
            argument,
            group_ref,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

/// Submits a priority task belonging to a task group.
///
/// Priority values:
///
/// - 0 = LOW
/// - 1 = NORMAL
/// - 2 = HIGH
///
/// Any other value returns INVALID_ARGUMENT.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_push_group_priority(
    scheduler: *mut CortletScheduler,
    group: *mut CortletTaskGroup,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
    priority: i32,
) -> CortletResult {
    if scheduler.is_null()
        || group.is_null()
    {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let Some(priority) =
        map_priority(
            priority,
        )
    else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    let group =
        unsafe {
            &*group
        };

    let group_ref =
        Arc::clone(
            &group.group,
        );

    match scheduler
        .scheduler
        .push_group_priority(
            task,
            argument,
            priority,
            group_ref,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

/// Waits until all tasks belonging to a task group have completed.
///
/// Returns INVALID_ARGUMENT when passed NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_task_group_wait(
    group: *mut CortletTaskGroup,
) -> CortletResult {
    if group.is_null() {
        return CortletResult::InvalidArgument;
    }

    let group =
        unsafe {
            &*group
        };

    group
        .group
        .wait();

    CortletResult::Ok
}

/// Destroys a task group's public C handle.
///
/// The group is closed before destruction so new tasks may no longer
/// be attached.
///
/// Already-submitted tasks remain valid because those tasks hold their
/// own Arc references.
///
/// Passing NULL is allowed.
///
/// This function must not run concurrently with another operation using
/// the same public task-group handle.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_task_group_destroy(
    group: *mut CortletTaskGroup,
) {
    if group.is_null() {
        return;
    }

    let group =
        unsafe {
            Box::from_raw(
                group,
            )
        };

    group
        .group
        .close();

    drop(
        group,
    );
}

/* ---------------------------------------------------------
 * Synchronization
 * --------------------------------------------------------- */

/// Blocks until all outstanding scheduler tasks have completed.
///
/// Multiple threads may wait concurrently as long as scheduler
/// destruction does not occur concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_wait(
    scheduler: *mut CortletScheduler,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    match scheduler
        .scheduler
        .wait()
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

/// Returns the configured maximum number of outstanding tasks.
///
/// Returns:
///
/// - 0 = unlimited
/// - non-zero = configured capacity
///
/// Passing NULL returns 0.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_queue_capacity(
    scheduler: *const CortletScheduler,
) -> usize {
    if scheduler.is_null() {
        return 0;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    scheduler
        .scheduler
        .queue_capacity()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_try_push(
    scheduler: *mut CortletScheduler,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    match scheduler
        .scheduler
        .try_push(
            task,
            argument,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_try_push_priority(
    scheduler: *mut CortletScheduler,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
    priority: i32,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let Some(priority) =
        map_priority(
            priority,
        )
    else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    match scheduler
        .scheduler
        .try_push_priority(
            task,
            argument,
            priority,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_try_push_group(
    scheduler: *mut CortletScheduler,
    group: *mut CortletTaskGroup,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
) -> CortletResult {
    if scheduler.is_null()
        || group.is_null()
    {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    let group =
        unsafe {
            &*group
        };

    let group_ref =
        Arc::clone(
            &group.group,
        );

    match scheduler
        .scheduler
        .try_push_group(
            task,
            argument,
            group_ref,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_try_push_group_priority(
    scheduler: *mut CortletScheduler,
    group: *mut CortletTaskGroup,
    task: Option<CortletTaskFn>,
    argument: *mut c_void,
    priority: i32,
) -> CortletResult {
    if scheduler.is_null()
        || group.is_null()
    {
        return CortletResult::InvalidArgument;
    }

    let Some(task) = task else {
        return CortletResult::InvalidArgument;
    };

    let Some(priority) =
        map_priority(
            priority,
        )
    else {
        return CortletResult::InvalidArgument;
    };

    let scheduler =
        unsafe {
            &*scheduler
        };

    let group =
        unsafe {
            &*group
        };

    let group_ref =
        Arc::clone(
            &group.group,
        );

    match scheduler
        .scheduler
        .try_push_group_priority(
            task,
            argument,
            priority,
            group_ref,
        )
    {
        Ok(()) => {
            CortletResult::Ok
        }

        Err(error) => {
            map_scheduler_error(
                error,
            )
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_get_stats(
    scheduler: *const CortletScheduler,
    stats: *mut CortletSchedulerStats,
) -> CortletResult {
    if scheduler.is_null()
        || stats.is_null()
    {
        return CortletResult::InvalidArgument;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    let snapshot =
        scheduler
            .scheduler
            .stats();

    let output =
        CortletSchedulerStats {
            submitted:
            snapshot.submitted,

            completed:
            snapshot.completed,

            rejected_full:
            snapshot.rejected_full,

            steals_attempted:
            snapshot.steals_attempted,

            steals_succeeded:
            snapshot.steals_succeeded,

            steal_failures:
            snapshot.steal_failures,

            fairness_yields:
            snapshot.fairness_yields,

            outstanding:
            scheduler
                .scheduler
                .tasks_in_flight(),
        };

    unsafe {
        *stats = output;
    }

    CortletResult::Ok
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_shutdown(
    scheduler: *mut CortletScheduler,
) -> CortletResult {
    if scheduler.is_null() {
        return CortletResult::InvalidArgument;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    scheduler
        .scheduler
        .shutdown();

    CortletResult::Ok
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_is_stopped(
    scheduler: *const CortletScheduler,
) -> i32 {
    if scheduler.is_null() {
        return 1;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    if scheduler
        .scheduler
        .is_stopped()
    {
        1
    } else {
        0
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_outstanding_tasks(
    scheduler: *const CortletScheduler,
) -> usize {
    if scheduler.is_null() {
        return 0;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    scheduler
        .scheduler
        .outstanding_tasks()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn cortlet_sched_sleeping_workers(
    scheduler: *const CortletScheduler,
) -> usize {
    if scheduler.is_null() {
        return 0;
    }

    let scheduler =
        unsafe {
            &*scheduler
        };

    scheduler
        .scheduler
        .sleeping_workers()
}
#[unsafe(no_mangle)]
pub extern "C" fn cortlet_sched_high_burst_limit(
) -> usize {
    Scheduler::high_burst_limit()
}