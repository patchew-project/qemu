//! Bindings to the raw low-level C API commonly provided by QEMU projects.
//!
//! Manual bindings to C API availabe when linking QEMU projects.
//! It includes minimal glib allocation functions too, since it's the default
//! allocator used by QEMU, and we don't depend on glib-rs crate yet).
//!
//! Higher-level Rust-friendly bindings are provided by different modules.

use libc::{c_char, c_void, size_t};

extern "C" {
    pub fn g_malloc0(n_bytes: size_t) -> *mut c_void;
    pub fn g_free(ptr: *mut c_void);
    pub fn g_strndup(str: *const c_char, n: size_t) -> *mut c_char;
}

#[repr(C)]
pub struct QObject(c_void);

impl ::std::fmt::Debug for QObject {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("QObject @ {:?}", self as *const _))
            .finish()
    }
}

#[repr(C)]
pub struct QNull(c_void);

impl ::std::fmt::Debug for QNull {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("QNull @ {:?}", self as *const _))
            .finish()
    }
}

#[repr(C)]
pub struct Error(c_void);

impl ::std::fmt::Debug for Error {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        f.debug_struct(&format!("Error @ {:?}", self as *const _))
            .finish()
    }
}

extern "C" {
    pub fn error_setg_internal(
        errp: *mut *mut Error,
        src: *const c_char,
        line: i32,
        func: *const c_char,
        fmt: *const c_char,
        ...
    );
    pub fn error_get_pretty(err: *const Error) -> *const c_char;
    pub fn error_free(err: *mut Error);
}

/// Wrap a QMP hanlder.
#[macro_export]
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
