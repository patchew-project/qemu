use std::{ffi::CStr, ptr, str};

use crate::{sys, translate};

/// A type representing an owned C QEMU Error.
pub struct CError(ptr::NonNull<sys::Error>);

impl translate::FromQemuPtrFull<*mut sys::Error> for CError {
    unsafe fn from_qemu_full(ptr: *mut sys::Error) -> Self {
        assert!(!ptr.is_null());
        Self(ptr::NonNull::new_unchecked(ptr))
    }
}

impl CError {
    pub fn pretty(&self) -> &str {
        unsafe {
            let pretty = sys::error_get_pretty(self.0.as_ptr());
            let bytes = CStr::from_ptr(pretty).to_bytes();
            str::from_utf8(bytes)
                .unwrap_or_else(|err| str::from_utf8(&bytes[..err.valid_up_to()]).unwrap())
        }
    }
}

impl Drop for CError {
    fn drop(&mut self) {
        unsafe { sys::error_free(self.0.as_ptr()) }
    }
}
