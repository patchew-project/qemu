//! Common code for QEMU
//!
//! This crates provides common bindings and facilities for QEMU C API shared by
//! various projects. Most importantly, it defines the conversion traits used to
//! convert from C to Rust types. Those traits are largely adapted from glib-rs,
//! since those have prooven to be very flexible, and should guide us to bind
//! further QEMU types such as QOM. If glib-rs becomes a dependency, we should
//! consider adopting glib translate traits. For QAPI, we need a smaller subset.

pub use libc;

mod error;
pub use error::*;

mod qemu;
pub use qemu::*;

mod translate;
pub use translate::*;

pub mod ffi;
