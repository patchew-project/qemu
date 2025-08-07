// Copyright (C) 2025 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for `rcu_read_lock` and `rcu_read_unlock`.
//! More details about RCU in QEMU, please refer docs/devel/rcu.rst.

use crate::bindings;

/// Used by a reader to inform the reclaimer that the reader is
/// entering an RCU read-side critical section.
pub fn rcu_read_lock() {
    // SAFETY: no return and no argument, everything is done at C side.
    unsafe { bindings::rcu_read_lock() }
}

/// Used by a reader to inform the reclaimer that the reader is
/// exiting an RCU read-side critical section.  Note that RCU
/// read-side critical sections may be nested and/or overlapping.
pub fn rcu_read_unlock() {
    // SAFETY: no return and no argument, everything is done at C side.
    unsafe { bindings::rcu_read_unlock() }
}

// FIXME: maybe we need rcu_read_lock_held() to check the rcu context,
// then make it possible to add assertion at any RCU critical section.
