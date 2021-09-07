use common::*;

use crate::{qapi::NewPtr, qapi_ffi};

mod hostname;

#[no_mangle]
extern "C" fn qmp_guest_get_host_name(errp: *mut *mut ffi::Error) -> *mut qapi_ffi::GuestHostName {
    qmp!(hostname::get(), errp)
}

mod vcpus;

#[no_mangle]
extern "C" fn qmp_guest_get_vcpus(
    errp: *mut *mut ffi::Error,
) -> *mut qapi_ffi::GuestLogicalProcessorList {
    qmp!(vcpus::get(), errp)
}

#[no_mangle]
extern "C" fn qmp_guest_set_vcpus(
    vcpus: *const qapi_ffi::GuestLogicalProcessorList,
    errp: *mut *mut ffi::Error,
) -> libc::c_longlong {
    let vcpus = unsafe { from_qemu_none(NewPtr(vcpus)) };
    qmp!(vcpus::set(vcpus), errp, -1)
}
