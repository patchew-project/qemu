use std::{self, ffi::CString, fmt, io, ptr};

use crate::translate::*;
use crate::{ffi, qemu};

/// Common error type for QEMU and related projects.
#[derive(Debug)]
pub enum Error {
    /// A generic error with file and line location.
    FailedAt(String, &'static str, u32),
    /// An IO error.
    Io(io::Error),
    #[cfg(unix)]
    /// A nix error.
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
    type QemuType = *mut ffi::Error;
}

impl<'a> ToQemuPtr<'a, *mut ffi::Error> for Error {
    type Storage = qemu::CError;

    fn to_qemu_none(&'a self) -> Stash<'a, *mut ffi::Error, Self> {
        let err = self.to_qemu_full();

        Stash(err, unsafe { from_qemu_full(err) })
    }

    fn to_qemu_full(&self) -> *mut ffi::Error {
        let cmsg =
            CString::new(self.message()).expect("ToQemuPtr<Error>: unexpected '\0' character");
        let mut csrc = CString::new("").unwrap();
        let (src, line) = self.location().map_or((ptr::null(), 0_i32), |loc| {
            csrc = CString::new(loc.0).expect("ToQemuPtr<Error>:: unexpected '\0' character");
            (csrc.as_ptr() as *const libc::c_char, loc.1 as i32)
        });
        let func = ptr::null();

        let mut err: *mut ffi::Error = ptr::null_mut();
        unsafe {
            ffi::error_setg_internal(
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

/// Convenience macro to build a [`Error::FailedAt`] error.
///
/// Returns a `Result::Err` with the file:line location.
/// (the error can then be converted to a QEMU `ffi::Error`)
#[allow(unused_macros)]
#[macro_export]
macro_rules! err {
    ($msg:expr) => {
        Err(Error::FailedAt($msg.into(), file!(), line!()))
    };
}
