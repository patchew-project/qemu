use std::convert::TryInto;
use std::error::Error;
use std::ffi::CString;
use std::os::unix::io::{AsRawFd, RawFd};
use std::ptr;

use zbus::fdo;
use zbus::{dbus_interface, Connection, DBusError, ObjectServer};

use crate::qapi;
use crate::qapi_sys;
use crate::qemu;
use crate::qemu_sys;
use crate::translate::*;

include!(concat!(env!("MESON_BUILD_ROOT"), "/qga/qga-qapi-dbus.rs"));

#[derive(Debug, DBusError)]
#[dbus_error(prefix = "org.qemu.QapiError")]
pub enum QapiError {
    /// ZBus error
    ZBus(zbus::Error),
    /// QMP error
    Failed(String),
}

impl FromQemuPtrFull<*mut qemu_sys::Error> for QapiError {
    unsafe fn from_qemu_full(ptr: *mut qemu_sys::Error) -> Self {
        QapiError::Failed(qemu::Error::from_raw(ptr).pretty().to_string())
    }
}

type Result<T> = std::result::Result<T, QapiError>;

#[derive(Debug)]
pub struct QemuDBus {
    pub connection: Connection,
    pub server: ObjectServer<'static>,
}

impl QemuDBus {
    fn open(name: &str) -> std::result::Result<Self, Box<dyn Error>> {
        let connection = Connection::new_session()?;

        fdo::DBusProxy::new(&connection)?
            .request_name(name, fdo::RequestNameFlags::ReplaceExisting.into())?;

        let server = ObjectServer::new(&connection);
        Ok(Self { connection, server })
    }
}

#[no_mangle]
extern "C" fn qemu_dbus_new() -> *mut QemuDBus {
    let mut dbus = match QemuDBus::open(&"org.qemu.qga") {
        Ok(dbus) => dbus,
        Err(e) => {
            eprintln!("{}", e);
            return std::ptr::null_mut();
        }
    };
    dbus.server
        .at(&"/org/qemu/qga".try_into().unwrap(), QgaQapi)
        .unwrap();

    Box::into_raw(Box::new(dbus))
}

#[no_mangle]
extern "C" fn qemu_dbus_free(dbus: *mut QemuDBus) {
    let dbus = unsafe {
        assert!(!dbus.is_null());
        Box::from_raw(dbus)
    };
    // let's be explicit:
    drop(dbus)
}

#[no_mangle]
extern "C" fn qemu_dbus_fd(dbus: *mut QemuDBus) -> RawFd {
    let dbus = unsafe {
        assert!(!dbus.is_null());
        &mut *dbus
    };

    dbus.connection.as_raw_fd()
}

#[no_mangle]
extern "C" fn qemu_dbus_next(dbus: *mut QemuDBus) {
    let dbus = unsafe {
        assert!(!dbus.is_null());
        &mut *dbus
    };

    if let Err(err) = dbus.server.try_handle_next() {
        eprintln!("{}", err);
    }
}
