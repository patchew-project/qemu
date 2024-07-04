// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

use core::ptr::NonNull;

use qemu_api::bindings::*;

use crate::{definitions::VMSTATE_PL011, device::PL011State};

qemu_api::declare_properties! {
    PL011_PROPERTIES,
    qemu_api::define_property!(
        c"chardev",
        PL011State,
        char_backend,
        unsafe { &qdev_prop_chr },
        CharBackend
    ),
    qemu_api::define_property!(
        c"migrate-clk",
        PL011State,
        migrate_clock,
        unsafe { &qdev_prop_bool },
        bool
    ),
}

qemu_api::device_class_init! {
    pl011_class_init,
    props => PL011_PROPERTIES,
    realize_fn => Some(pl011_realize),
    reset_fn => Some(pl011_reset),
    vmsd => VMSTATE_PL011,
}

#[no_mangle]
pub unsafe extern "C" fn pl011_realize(dev: *mut DeviceState, _errp: *mut *mut Error) {
    assert!(!dev.is_null());
    let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
    state.as_mut().realize();
}

#[no_mangle]
pub unsafe extern "C" fn pl011_reset(dev: *mut DeviceState) {
    assert!(!dev.is_null());
    let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
    state.as_mut().reset();
}
