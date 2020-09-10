use std::ffi::CStr;
/// or do something full-fledged like glib-rs boxed MM...
use std::ptr;
use std::str;

use crate::qemu_sys;

pub struct Error(ptr::NonNull<qemu_sys::Error>);

impl Error {
    pub unsafe fn from_raw(ptr: *mut qemu_sys::Error) -> Self {
        assert!(!ptr.is_null());
        Self(ptr::NonNull::new_unchecked(ptr))
    }

    pub fn pretty(&self) -> &str {
        unsafe {
            let pretty = qemu_sys::error_get_pretty(self.0.as_ptr());
            let bytes = CStr::from_ptr(pretty).to_bytes();
            str::from_utf8(bytes)
                .unwrap_or_else(|err| str::from_utf8(&bytes[..err.valid_up_to()]).unwrap())
        }
    }
}

impl Drop for Error {
    fn drop(&mut self) {
        unsafe { qemu_sys::error_free(self.0.as_ptr()) }
    }
}
