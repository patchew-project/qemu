use std::{self, ffi, fmt, io, ptr};

use crate::translate::*;
use crate::{qemu, sys};

/// Common error type for QEMU and related projects.
#[derive(Debug)]
pub enum Error {
    FailedAt(String, &'static str, u32),
    Io(io::Error),
    #[cfg(unix)]
    Nix(nix::Error),
}

/// Alias for a `Result` with the error type for QEMU.
pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    fn message(&self) -> String {
        use Error::*;
        match self {
            FailedAt(msg, _, _) => msg.into(),
            Io(io) => format!("IO error: {}", io),
            #[cfg(unix)]
            Nix(nix) => format!("Nix error: {}", nix),
        }
    }

    fn location(&self) -> Option<(&'static str, u32)> {
        use Error::*;
        match self {
            FailedAt(_, file, line) => Some((file, *line)),
            Io(_) => None,
            #[cfg(unix)]
            Nix(_) => None,
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            FailedAt(msg, file, line) => write!(f, "{} ({}:{})", msg, file, line),
            _ => write!(f, "{}", self.message()),
        }
    }
}

impl From<io::Error> for Error {
    fn from(val: io::Error) -> Self {
        Error::Io(val)
    }
}

#[cfg(unix)]
impl From<nix::Error> for Error {
    fn from(val: nix::Error) -> Self {
        Error::Nix(val)
    }
}

impl QemuPtrDefault for Error {
    type QemuType = *mut sys::Error;
}

impl<'a> ToQemuPtr<'a, *mut sys::Error> for Error {
    type Storage = qemu::CError;

    fn to_qemu_none(&'a self) -> Stash<'a, *mut sys::Error, Self> {
        let err = self.to_qemu_full();

        Stash(err, unsafe { from_qemu_full(err) })
    }

    fn to_qemu_full(&self) -> *mut sys::Error {
        let cmsg =
            ffi::CString::new(self.message()).expect("ToQemuPtr<Error>: unexpected '\0' character");
        let mut csrc = ffi::CString::new("").unwrap();
        let (src, line) = self.location().map_or((ptr::null(), 0 as i32), |loc| {
            csrc = ffi::CString::new(loc.0).expect("ToQemuPtr<Error>:: unexpected '\0' character");
            (csrc.as_ptr() as *const libc::c_char, loc.1 as i32)
        });
        let func = ptr::null();

        let mut err: *mut sys::Error = ptr::null_mut();
        unsafe {
            sys::error_setg_internal(
                &mut err as *mut *mut _,
                src,
                line,
                func,
                cmsg.as_ptr() as *const libc::c_char,
            );
            err
        }
    }
}

/// Convenience macro to build a `Error::FailedAt` error.
///
/// (this error type can be nicely converted to a QEMU `sys::Error`)
#[allow(unused_macros)]
#[macro_export]
macro_rules! err {
    ($err:expr) => {
        Err(Error::FailedAt($err.into(), file!(), line!()))
    };
}
