// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

//! Definitions required by QEMU when registering the device.

use core::{mem::MaybeUninit, ptr::NonNull};

use qemu_api::bindings::*;

use crate::{device::PL011State, device_class::pl011_class_init};

qemu_api::type_info! {
    PL011_ARM_INFO: PL011State,
    name: c"x-pl011-rust",
    parent: TYPE_SYS_BUS_DEVICE,
    instance_init: Some(pl011_init),
    abstract_: false,
    class_init: Some(pl011_class_init),
}

#[used]
pub static VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: PL011_ARM_INFO.name,
    unmigratable: true,
    ..unsafe { MaybeUninit::<VMStateDescription>::zeroed().assume_init() }
};

#[no_mangle]
pub unsafe extern "C" fn pl011_init(obj: *mut Object) {
    assert!(!obj.is_null());
    let mut state = NonNull::new_unchecked(obj.cast::<PL011State>());
    state.as_mut().init();
}

qemu_api::module_init! {
    qom: register_type => {
        type_register_static(&PL011_ARM_INFO);
    }
}
