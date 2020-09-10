#![allow(dead_code)]
use std::convert::{TryFrom, TryInto};
use std::{ptr, str};

#[cfg(feature = "dbus")]
use zvariant::OwnedValue;
#[cfg(feature = "dbus")]
use serde::{Deserialize, Serialize};
#[cfg(feature = "dbus")]
use zvariant_derive::{Type, TypeDict, SerializeDict, DeserializeDict};

use crate::translate::*;

use crate::translate;
use crate::qapi_sys;
use crate::qemu_sys;

include!(concat!(env!("MESON_BUILD_ROOT"), "/qga/qga-qapi-types.rs"));

impl<'a> ToQemuPtr<'a, *mut qapi_sys::GuestFileWhence> for GuestFileWhence {
    type Storage = Box<qapi_sys::GuestFileWhence>;

    #[inline]
    fn to_qemu_none(&'a self) -> Stash<'a, *mut qapi_sys::GuestFileWhence, GuestFileWhence> {
        let mut w = Box::new(self.into());

        Stash(&mut *w, w)
    }
}

impl From<&GuestFileWhence> for qapi_sys::GuestFileWhence {
    fn from(w: &GuestFileWhence) -> Self {
        match *w {
            GuestFileWhence::Name(name) => Self {
                ty: QType::Qstring,
                u: qapi_sys::GuestFileWhenceUnion { name },
            },
            GuestFileWhence::Value(value) => Self {
                ty: QType::Qnum,
                u: qapi_sys::GuestFileWhenceUnion { value },
            },
        }
    }
}

#[cfg(feature = "dbus")]
impl From<GuestFileWhence> for OwnedValue {
    fn from(_w: GuestFileWhence) -> Self {
        unimplemented!()
    }
}

#[cfg(feature = "dbus")]
impl TryFrom<OwnedValue> for GuestFileWhence {
    type Error = &'static str;

    fn try_from(value: OwnedValue) -> Result<Self, Self::Error> {
        if let Ok(val) = (&value).try_into() {
            return Ok(Self::Name(match val {
                "set" => QGASeek::Set,
                "cur" => QGASeek::Cur,
                "end" => QGASeek::End,
                _ => return Err("Invalid seek value"),
            }));
        }
        if let Ok(val) = value.try_into() {
            return Ok(Self::Value(val));
        };
        Err("Invalid whence")
    }
}

macro_rules! vec_to_qemu_ptr {
    ($rs:ident, $sys:ident) => {
        #[allow(non_camel_case_types)]
        pub struct $sys(*mut qapi_sys::$sys);

        impl Drop for $sys {
            fn drop(&mut self) {
                let mut list = self.0;
                unsafe {
                    while !list.is_null() {
                        let next = (*list).next;
                        Box::from_raw(list);
                        list = next;
                    }
                }
            }
        }

        impl<'a> ToQemuPtr<'a, *mut qapi_sys::$sys> for Vec<$rs> {
            type Storage = (
                Option<$sys>,
                Vec<Stash<'a, <$rs as QemuPtrDefault>::QemuType, $rs>>,
            );

            #[inline]
            fn to_qemu_none(&self) -> Stash<*mut qapi_sys::$sys, Self> {
                let stash_vec: Vec<_> = self.iter().rev().map(ToQemuPtr::to_qemu_none).collect();
                let mut list: *mut qapi_sys::$sys = ptr::null_mut();
                for stash in &stash_vec {
                    let b = Box::new(qapi_sys::$sys {
                        next: list,
                        value: Ptr::to(stash.0),
                    });
                    list = Box::into_raw(b);
                }
                Stash(list, (Some($sys(list)), stash_vec))
            }
        }
    };
}

// TODO: could probably be templated instead
vec_to_qemu_ptr!(String, strList);
vec_to_qemu_ptr!(GuestAgentCommandInfo, GuestAgentCommandInfoList);
vec_to_qemu_ptr!(GuestFilesystemTrimResult, GuestFilesystemTrimResultList);
vec_to_qemu_ptr!(GuestIpAddress, GuestIpAddressList);
vec_to_qemu_ptr!(GuestDiskAddress, GuestDiskAddressList);
vec_to_qemu_ptr!(GuestLogicalProcessor, GuestLogicalProcessorList);
vec_to_qemu_ptr!(GuestMemoryBlock, GuestMemoryBlockList);
