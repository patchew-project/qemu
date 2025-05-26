// SPDX-License-Identifier: GPL-2.0-or-later

//! Error class for QEMU Rust code
//!
//! @author Paolo Bonzini

use std::{
    borrow::Cow,
    ffi::{c_char, c_int, c_void, CStr},
    fmt::{self, Display},
    panic, ptr,
};

use foreign::{prelude::*, OwnedPointer};

use crate::{
    bindings,
    bindings::{error_free, error_get_pretty},
};

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, Default)]
pub struct Error {
    msg: Option<Cow<'static, str>>,
    /// Appends the print string of the error to the msg if not None
    cause: Option<anyhow::Error>,
    file: &'static str,
    line: u32,
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.cause.as_ref().map(AsRef::as_ref)
    }

    #[allow(deprecated)]
    fn description(&self) -> &str {
        self.msg
            .as_deref()
            .or_else(|| self.cause.as_deref().map(std::error::Error::description))
            .unwrap_or("error")
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut prefix = "";
        if let Some(ref msg) = self.msg {
            write!(f, "{msg}")?;
            prefix = ": ";
        }
        if let Some(ref cause) = self.cause {
            write!(f, "{prefix}{cause}")?;
        } else if prefix.is_empty() {
            f.write_str("unknown error")?;
        }
        Ok(())
    }
}

impl From<String> for Error {
    #[track_caller]
    fn from(msg: String) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(Cow::Owned(msg)),
            file: location.file(),
            line: location.line(),
            ..Default::default()
        }
    }
}

impl From<&'static str> for Error {
    #[track_caller]
    fn from(msg: &'static str) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(Cow::Borrowed(msg)),
            file: location.file(),
            line: location.line(),
            ..Default::default()
        }
    }
}

impl From<anyhow::Error> for Error {
    #[track_caller]
    fn from(error: anyhow::Error) -> Self {
        let location = panic::Location::caller();
        Error {
            cause: Some(error),
            file: location.file(),
            line: location.line(),
            ..Default::default()
        }
    }
}

impl Error {
    /// Create a new error, prepending `msg` to the
    /// description of `cause`
    #[track_caller]
    pub fn with_error<E: std::error::Error + Send + Sync + 'static>(msg: &'static str, cause: E) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(Cow::Borrowed(msg)),
            cause: Some(cause.into()),
            file: location.file(),
            line: location.line(),
        }
    }

    /// Consume a result, returning false if it is an error and
    /// true if it is successful.  The error is propagated into
    /// `errp` like the C API `error_propagate` would do.
    ///
    /// # Safety
    ///
    /// `errp` must be valid; typically it is received from C code
    pub unsafe fn bool_or_propagate(result: Result<()>, errp: *mut *mut bindings::Error) -> bool {
        // SAFETY: caller guarantees errp is valid
        unsafe { Self::ok_or_propagate(result, errp) }.is_some()
    }

    /// Consume a result, returning a `NULL` pointer if it is an
    /// error and a C representation of the contents if it is
    /// successful.  The error is propagated into `errp` like
    /// the C API `error_propagate` would do.
    ///
    /// # Safety
    ///
    /// `errp` must be valid; typically it is received from C code
    #[must_use]
    pub unsafe fn ptr_or_propagate<T: CloneToForeign>(
        result: Result<T>,
        errp: *mut *mut bindings::Error,
    ) -> *mut T::Foreign {
        // SAFETY: caller guarantees errp is valid
        unsafe { Self::ok_or_propagate(result, errp) }.clone_to_foreign_ptr()
    }

    /// Consume a result in the same way as `self.ok()`, but also propagate
    /// a possible error into `errp`, like the C API `error_propagate`
    /// would do.
    ///
    /// # Safety
    ///
    /// `errp` must be valid; typically it is received from C code
    pub unsafe fn ok_or_propagate<T>(
        result: Result<T>,
        errp: *mut *mut bindings::Error,
    ) -> Option<T> {
        let Err(err) = result else {
            return result.ok();
        };

        // SAFETY: caller guarantees errp is valid
        unsafe {
            err.propagate(errp);
        }
        None
    }

    /// Equivalent of the C function `error_propagate`.  Fill `*errp`
    /// with the information container in `result` if `errp` is not NULL;
    /// then consume it.
    ///
    /// # Safety
    ///
    /// `errp` must be valid; typically it is received from C code
    pub unsafe fn propagate(self, errp: *mut *mut bindings::Error) {
        if errp.is_null() {
            return;
        }

        let err = self.clone_to_foreign_ptr();

        // SAFETY: caller guarantees errp is valid
        unsafe {
            errp.write(err);
        }
    }

    /// Convert a C `Error*` into a Rust `Result`, using
    /// `Ok(())` if `c_error` is NULL.
    ///
    /// # Safety
    ///
    /// `c_error` must be valid; typically it has been filled by a C
    /// function.
    pub unsafe fn err_or_unit(c_error: *mut bindings::Error) -> Result<()> {
        // SAFETY: caller guarantees c_error is valid
        unsafe { Self::err_or_else(c_error, || ()) }
    }

    /// Convert a C `Error*` into a Rust `Result`, calling `f()` to
    /// obtain an `Ok` value if `c_error` is NULL.
    ///
    /// # Safety
    ///
    /// `c_error` must be valid; typically it has been filled by a C
    /// function.
    pub unsafe fn err_or_else<T, F: FnOnce() -> T>(
        c_error: *mut bindings::Error,
        f: F,
    ) -> Result<T> {
        // SAFETY: caller guarantees c_error is valid
        let err = unsafe { Option::<Self>::from_foreign(c_error) };
        match err {
            None => Ok(f()),
            Some(err) => Err(err),
        }
    }
}

impl FreeForeign for Error {
    type Foreign = bindings::Error;

    unsafe fn free_foreign(p: *mut bindings::Error) {
        // SAFETY: caller guarantees p is valid
        unsafe {
            error_free(p);
        }
    }
}

impl CloneToForeign for Error {
    fn clone_to_foreign(&self) -> OwnedPointer<Self> {
        // SAFETY: all arguments are controlled by this function
        unsafe {
            let err: *mut c_void = libc::malloc(std::mem::size_of::<bindings::Error>());
            let err: &mut bindings::Error = &mut *err.cast();
            *err = bindings::Error {
                msg: format!("{self}").clone_to_foreign_ptr(),
                err_class: bindings::ERROR_CLASS_GENERIC_ERROR,
                src_len: self.file.len() as isize,
                src: self.file.as_ptr().cast::<c_char>(),
                line: self.line as c_int,
                func: ptr::null_mut(),
                hint: ptr::null_mut(),
            };
            OwnedPointer::new(err)
        }
    }
}

impl FromForeign for Error {
    unsafe fn cloned_from_foreign(c_error: *const bindings::Error) -> Self {
        // SAFETY: caller guarantees c_error is valid
        unsafe {
            let error = &*c_error;
            let file = if error.src_len < 0 {
                // NUL-terminated
                CStr::from_ptr(error.src).to_str()
            } else {
                // Can become str::from_utf8 with Rust 1.87.0
                std::str::from_utf8(std::slice::from_raw_parts(
                    &*error.src.cast::<u8>(),
                    error.src_len as usize,
                ))
            };

            Error {
                msg: FromForeign::cloned_from_foreign(error_get_pretty(error)),
                cause: None,
                file: file.unwrap(),
                line: error.line as u32,
            }
        }
    }
}
