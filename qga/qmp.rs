use std::ptr;

use crate::error::Result;
use crate::qapi;
use crate::qapi_sys;
use crate::qemu_sys;
use crate::translate::*;

macro_rules! qmp {
    // the basic return value variant
    ($e:expr, $errp:ident, $errval:expr) => {{
        assert!(!$errp.is_null());
        unsafe {
            *$errp = ptr::null_mut();
        }

        match $e {
            Ok(val) => val,
            Err(err) => unsafe {
                *$errp = err.to_qemu_full();
                $errval
            },
        }
    }};
    // the ptr return value variant
    ($e:expr, $errp:ident) => {{
        assert!(!$errp.is_null());
        unsafe {
            *$errp = ptr::null_mut();
        }

        match $e {
            Ok(val) => val.to_qemu_full(),
            Err(err) => unsafe {
                *$errp = err.to_qemu_full();
                ptr::null_mut()
            },
        }
    }};
}

fn guest_host_name() -> Result<qapi::GuestHostName> {
    Ok(qapi::GuestHostName {
        host_name: hostname::get()?.into_string().or(err!("Invalid hostname"))?,
    })
}

#[no_mangle]
extern "C" fn qmp_guest_get_host_name(
    errp: *mut *mut qemu_sys::Error,
) -> *mut qapi_sys::GuestHostName {
    qmp!(guest_host_name(), errp)
}

fn guest_set_vcpus(vcpus: Vec<qapi::GuestLogicalProcessor>) -> Result<i64> {
    dbg!(vcpus);
    err!("unimplemented")
}

#[no_mangle]
extern "C" fn qmp_guest_set_vcpus(
    vcpus: *const qapi_sys::GuestLogicalProcessorList,
    errp: *mut *mut qemu_sys::Error,
) -> libc::c_longlong {
    let vcpus = unsafe { from_qemu_none(vcpus) };
    qmp!(guest_set_vcpus(vcpus), errp, -1)
}
