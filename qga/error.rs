use std::{self, ffi, fmt, ptr, io};

use crate::qemu;
use crate::qemu_sys;
use crate::translate::*;

#[derive(Debug)]
pub enum Error {
    FailedAt(String, &'static str, u32),
    Io(io::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    fn message(&self) -> String {
        use Error::*;
        match self {
            FailedAt(msg, _, _) => msg.into(),
            Io(io) => format!("IO error: {}", io),
        }
    }

    fn location(&self) -> Option<(&'static str, u32)> {
        use Error::*;
        match self {
            FailedAt(_, file, line) => Some((file, *line)),
            Io(_) => None,
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            FailedAt(msg, file, line) => write!(f, "Failed: {} ({}:{})", msg, file, line),
            Io(io) => write!(f, "IO error: {}", io),
        }
    }
}

impl From<io::Error> for Error {
    fn from(val: io::Error) -> Self {
        Error::Io(val)
    }
}

impl QemuPtrDefault for Error {
    type QemuType = *mut qemu_sys::Error;
}

impl<'a> ToQemuPtr<'a, *mut qemu_sys::Error> for Error {
    type Storage = qemu::Error;

    fn to_qemu_none(&'a self) -> Stash<'a, *mut qemu_sys::Error, Self> {
        let err = self.to_qemu_full();

        Stash(err, unsafe { qemu::Error::from_raw(err) })
    }

    fn to_qemu_full(&self) -> *mut qemu_sys::Error {
        let cmsg =
            ffi::CString::new(self.message()).expect("ToQemuPtr<Error>: unexpected '\0' character");
        let mut csrc = ffi::CString::new("").unwrap();
        let (src, line) = self.location().map_or((ptr::null(), 0 as i32), |loc| {
            csrc = ffi::CString::new(loc.0).expect("ToQemuPtr<Error>:: unexpected '\0' character");
            (csrc.as_ptr() as *const libc::c_char, loc.1 as i32)
        });
        let func = ptr::null();

        let mut err: *mut qemu_sys::Error = ptr::null_mut();
        unsafe {
            qemu_sys::error_setg_internal(
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

macro_rules! err {
    ($err:expr) => {
        Err(crate::error::Error::FailedAt($err.into(), file!(), line!()))
    };
}
