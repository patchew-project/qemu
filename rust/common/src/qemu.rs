use std::{ffi::CStr, ptr, str};

use crate::{ffi, translate};
use translate::{FromQemuPtrFull, FromQemuPtrNone, QemuPtrDefault, Stash, ToQemuPtr};

/// A type representing an owned C QEMU Error.
pub struct CError(ptr::NonNull<ffi::Error>);

impl translate::FromQemuPtrFull<*mut ffi::Error> for CError {
    unsafe fn from_qemu_full(ptr: *mut ffi::Error) -> Self {
        assert!(!ptr.is_null());
        Self(ptr::NonNull::new_unchecked(ptr))
    }
}

impl CError {
    pub fn pretty(&self) -> &str {
        unsafe {
            let pretty = ffi::error_get_pretty(self.0.as_ptr());
            let bytes = CStr::from_ptr(pretty).to_bytes();
            str::from_utf8(bytes)
                .unwrap_or_else(|err| str::from_utf8(&bytes[..err.valid_up_to()]).unwrap())
        }
    }
}

impl Drop for CError {
    fn drop(&mut self) {
        unsafe { ffi::error_free(self.0.as_ptr()) }
    }
}

/// QObject (JSON object)
#[derive(Clone, Debug)]
pub struct QObject;

impl QemuPtrDefault for QObject {
    type QemuType = *mut ffi::QObject;
}

impl FromQemuPtrFull<*mut ffi::QObject> for QObject {
    #[inline]
    unsafe fn from_qemu_full(_ffi: *mut ffi::QObject) -> Self {
        unimplemented!()
    }
}

impl FromQemuPtrNone<*const ffi::QObject> for QObject {
    #[inline]
    unsafe fn from_qemu_none(_ffi: *const ffi::QObject) -> Self {
        unimplemented!()
    }
}

impl<'a> ToQemuPtr<'a, *mut ffi::QObject> for QObject {
    type Storage = ();

    #[inline]
    fn to_qemu_none(&self) -> Stash<'a, *mut ffi::QObject, QObject> {
        unimplemented!()
    }
    #[inline]
    fn to_qemu_full(&self) -> *mut ffi::QObject {
        unimplemented!()
    }
}

/// QNull (JSON null)
#[derive(Clone, Debug)]
pub struct QNull;

impl QemuPtrDefault for QNull {
    type QemuType = *mut ffi::QNull;
}

impl FromQemuPtrFull<*mut ffi::QObject> for QNull {
    #[inline]
    unsafe fn from_qemu_full(_ffi: *mut ffi::QObject) -> Self {
        unimplemented!()
    }
}

impl FromQemuPtrNone<*const ffi::QObject> for QNull {
    #[inline]
    unsafe fn from_qemu_none(_ffi: *const ffi::QObject) -> Self {
        unimplemented!()
    }
}

impl<'a> ToQemuPtr<'a, *mut ffi::QNull> for QNull {
    type Storage = ();

    #[inline]
    fn to_qemu_none(&self) -> Stash<'a, *mut ffi::QNull, QNull> {
        unimplemented!()
    }
    #[inline]
    fn to_qemu_full(&self) -> *mut ffi::QNull {
        unimplemented!()
    }
}
