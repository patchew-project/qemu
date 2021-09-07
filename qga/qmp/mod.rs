use common::*;

use crate::qapi_ffi;

mod hostname;

#[no_mangle]
extern "C" fn qmp_guest_get_host_name(errp: *mut *mut ffi::Error) -> *mut qapi_ffi::GuestHostName {
    qmp!(hostname::get(), errp)
}
