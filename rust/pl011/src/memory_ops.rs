// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

use core::{mem::MaybeUninit, ptr::NonNull};

use qemu_api::bindings::*;

use crate::device::PL011State;

pub static PL011_OPS: MemoryRegionOps = MemoryRegionOps {
    read: Some(pl011_read),
    write: Some(pl011_write),
    read_with_attrs: None,
    write_with_attrs: None,
    endianness: device_endian_DEVICE_NATIVE_ENDIAN,
    valid: unsafe { MaybeUninit::<MemoryRegionOps__bindgen_ty_1>::zeroed().assume_init() },
    impl_: MemoryRegionOps__bindgen_ty_2 {
        min_access_size: 4,
        max_access_size: 4,
        ..unsafe { MaybeUninit::<MemoryRegionOps__bindgen_ty_2>::zeroed().assume_init() }
    },
};

#[no_mangle]
unsafe extern "C" fn pl011_read(
    opaque: *mut core::ffi::c_void,
    addr: hwaddr,
    size: core::ffi::c_uint,
) -> u64 {
    assert!(!opaque.is_null());
    let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
    state.as_mut().read(addr, size)
}

#[no_mangle]
unsafe extern "C" fn pl011_write(
    opaque: *mut core::ffi::c_void,
    addr: hwaddr,
    data: u64,
    _size: core::ffi::c_uint,
) {
    assert!(!opaque.is_null());
    let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
    state.as_mut().write(addr, data)
}
