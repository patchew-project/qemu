// Copyright (C) 2026 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for `rcu_read_lock` and `rcu_read_unlock`.
//! More details about RCU in QEMU, please refer docs/devel/rcu.rst.

use std::{
    marker::PhantomData,
    sync::atomic::{AtomicPtr, Ordering},
};

use crate::bindings;

/// RAII guard for the RCU read side lock.  The lock is acquired on creation and
/// released on drop.
///
/// This guard is not `Send` or `Sync`, so it cannot be moved to another thread
/// or stored in a global variable.  This ensures the RCU read side lock to be
/// held and unlocked in the same thread.  The guard is also not `Clone` or
/// `Copy`, so it cannot be duplicated and this can avoid double-unlock, which
/// would be a bug.
///
/// ```ignore
/// let guard = RcuGuard::new();
/// // do some RCU read-side work
/// drop(guard); // releases the RCU read side lock
/// ```
pub struct RcuGuard {
    _marker: PhantomData<*const ()>,
}

impl RcuGuard {
    /// Acquires the RCU read side lock and returns a guard.
    #[inline]
    pub fn new() -> Self {
        // SAFETY: An FFI call with no additional requirements.
        unsafe {
            bindings::rust_rcu_read_lock();
        }
        // INVARIANT: The RCU read side lock was just acquired above.
        Self {
            _marker: PhantomData,
        }
    }
}

impl Default for RcuGuard {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for RcuGuard {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: An FFI call with no additional requirements.
        unsafe {
            bindings::rust_rcu_read_unlock();
        }
    }
}

/// (Reader-side view of) an RCU-protected C pointer wrapper.
///
/// This wrapper expresses the RCU read semantics of the C side:
///
/// ```c
/// rcu_read_lock();
/// p = qatomic_rcu_read(&foo);
/// /* do something with p. */
/// rcu_read_unlock();
/// ```
///
/// With this wrapper, the above code can be expressed as:
///
/// ```ignore
/// // SAFETY: `foo_ptr` is a live RCU-protected pointer.
/// let cell = unsafe { RcuCell::from_ptr(foo_ptr) };
/// {
///     let guard = RcuGuard::new();
///     if let Some(p) = cell.get(&guard) {
///         /* do something with p. */
///     }
/// } // `rcu_read_unlock()` runs here, when `guard` is dropped.
/// ```
///
/// # Ordering vs. C's `qatomic_rcu_read`
///
/// C's `qatomic_rcu_read` compiles to `__ATOMIC_RELAXED` followed by
/// `smp_read_barrier_depends()` (or `__ATOMIC_CONSUME` under TSAN).
/// `smp_read_barrier_depends()` is a *compiler* barrier on all hosts QEMU
/// supports (see [docs/devel/atomics.rst](../../docs/devel/atomics.rst)),
/// providing dependency ordering.
///
/// Rust exposes neither `Ordering::Consume` nor a dependency-barrier
/// primitive.  So there is no exact Rust equivalent of `qatomic_rcu_read`.
/// Instead, use [`Ordering::Acquire`], the strictly stronger option:
/// - `Acquire` is a correct upper bound: every ordering that C's
///   `qatomic_rcu_read` guarantees is also guaranteed by `Acquire`, and it
///   correctly pairs with the updater's `qatomic_rcu_set()` (a Release store).
#[repr(transparent)]
pub struct RcuCell<T> {
    data: AtomicPtr<T>,
}

impl<T> RcuCell<T> {
    /// Convert a C raw pointer to an [`RcuCell`] in place, without copying it.
    ///
    /// Note: copying the pointer first and then loading the copy would
    /// synchronise with nothing: the `Ordering::Acquire` must be applied to
    /// the original pointer to pair with `qatomic_rcu_set()`.
    ///
    /// # Safety
    ///
    /// `field` must point to a valid `*mut T` updated only with
    /// `qatomic_rcu_set()`, and the `T` object pointed to by `*field` must
    ///  remain valid during the RCU read-side critical section entered before
    ///  calling `RcuCell<T>::get`.
    pub unsafe fn from_ptr<'a>(field: *const *mut T) -> &'a Self {
        // SAFETY: `RcuCell` is `repr(transparent)` over `AtomicPtr<T>`.
        unsafe { &*(field.cast::<Self>()) }
    }

    /// An almost equivalent of `qatomic_rcu_read()`, but with
    /// `Ordering::Acquire`.
    #[inline]
    fn raw_get(&self) -> *mut T {
        self.data.load(Ordering::Acquire)
    }

    pub fn get<'g>(&self, _: &'g RcuGuard) -> Option<&'g T> {
        // SAFETY: the reference is *shared*, which allows several readers
        // to hold the RCU lock concurrently.
        unsafe { self.raw_get().as_ref() }
    }
}
