//! `QObject` serialization
//!
//! This module implements the [`Serialize`] trait for `QObject`,
//! allowing it to be converted to other formats, for example
//! JSON.

use std::{ffi::CStr, mem::ManuallyDrop, ptr::addr_of};

use serde::ser::{self, Serialize, SerializeMap, SerializeSeq};

use super::{match_qobject, QObject};
use crate::bindings;

impl Serialize for QObject {
    #[inline]
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: ::serde::Serializer,
    {
        match_qobject! { (self) =>
            () => serializer.serialize_unit(),
            bool(b) => serializer.serialize_bool(b),
            i64(i) => serializer.serialize_i64(i),
            u64(u) => serializer.serialize_u64(u),
            f64(f) => serializer.serialize_f64(f),
            CStr(cstr) => cstr.to_str().map_or_else(
                |_| Err(ser::Error::custom("invalid UTF-8 in QString")),
                |s| serializer.serialize_str(s),
            ),
            QList(l) => {
                let mut node_ptr = unsafe { l.head.tqh_first };
                let mut state = serializer.serialize_seq(None)?;
                while !node_ptr.is_null() {
                    let node = unsafe { &*node_ptr };
                    let elem = unsafe { ManuallyDrop::new(QObject::from_raw(addr_of!(*node.value))) };
                    state.serialize_element(&*elem)?;
                    node_ptr = unsafe { node.next.tqe_next };
                }
                state.end()
            },
            QDict(d) => {
                let mut state = serializer.serialize_map(Some(d.size))?;
                let mut e_ptr = unsafe { bindings::qdict_first(d) };
                while !e_ptr.is_null() {
                    let e = unsafe { &*e_ptr };
                    let key = unsafe { CStr::from_ptr(e.key) };
                    key.to_str().map_or_else(
                        |_| Err(ser::Error::custom("invalid UTF-8 in key")),
                        |k| state.serialize_key(k),
                    )?;
                    let value = unsafe { ManuallyDrop::new(QObject::from_raw(addr_of!(*e.value))) };
                    state.serialize_value(&*value)?;
                    e_ptr = unsafe { bindings::qdict_next(d, e) };
                }
                state.end()
            }
        }
    }
}
