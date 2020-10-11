use common::*;

use crate::*;

macro_rules! qmp {
    // the basic return value variant
    ($e:expr, $errp:ident, $errval:expr) => {{
        assert!(!$errp.is_null());
        unsafe {
            *$errp = std::ptr::null_mut();
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
            *$errp = std::ptr::null_mut();
        }

        match $e {
            Ok(val) => val.to_qemu_full().into(),
            Err(err) => unsafe {
                *$errp = err.to_qemu_full();
                std::ptr::null_mut()
            },
        }
    }};
}

mod hostname;

#[no_mangle]
extern "C" fn qmp_guest_get_host_name(errp: *mut *mut sys::Error) -> *mut qapi_sys::GuestHostName {
    qmp!(hostname::get(), errp)
}
