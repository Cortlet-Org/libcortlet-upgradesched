//! libcortlet-upgradesched v2
//!
//! High-performance Linux task scheduler with a stable C-compatible API.
//!
//! The public ABI is exposed through `ffi.rs`. Internal scheduler
//! implementation details remain private to the Rust crate.

#![forbid(unsafe_op_in_unsafe_fn)]
#![deny(
    missing_debug_implementations,
    unused_must_use,
    unreachable_pub,
    rust_2018_idioms
)]

#[cfg(not(target_os = "linux"))]
compile_error!("libcortlet-upgradesched v2 currently supports Linux only.");

mod ffi;
mod queue;
mod scheduler;
mod steal;
mod topology;
mod worker;
mod group;
mod stats;

// Re-export only the C ABI surface.
//
// Internal scheduler types should stay private so the implementation
// can evolve without affecting the exported C interface.
pub use ffi::*;