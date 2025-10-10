//! `QObject` bindings
//!
//! This module implements bindings for QEMU's `QObject` data structure.
//! The bindings integrate with `serde`, which take the role of visitors
//! in Rust code.

#![deny(clippy::unwrap_used)]

mod deserialize;
mod deserializer;
mod error;
mod serialize;
mod serializer;

use core::fmt::{self, Debug, Display};
use std::{
    cell::UnsafeCell,
    ffi::{c_char, CString},
    mem::ManuallyDrop,
    ptr::{addr_of, addr_of_mut},
    sync::atomic::{AtomicUsize, Ordering},
};

use common::assert_field_type;
pub use deserializer::from_qobject;
pub use error::{Error, Result};
use foreign::prelude::*;
pub use serializer::to_qobject;

use crate::bindings;

/// A wrapper for a C `QObject`.
///
/// Because `QObject` is not thread-safe, the safety of these bindings
/// right now hinges on treating them as immutable.  It is part of the
/// contract with the `QObject` constructors that the Rust struct is
/// only built after the contents are stable.
///
/// Only a bare bones API is public; production and consumption of `QObject`
/// generally goes through `serde`.
pub struct QObject(&'static UnsafeCell<bindings::QObject>);

// SAFETY: the QObject API are not thread-safe other than reference counting;
// but the Rust struct is only created once the contents are stable, and
// therefore it obeys the aliased XOR mutable invariant.
unsafe impl Send for QObject {}
unsafe impl Sync for QObject {}

// Since a QObject can be a floating-point value, and potentially a NaN,
// do not implement Eq
impl PartialEq for QObject {
    fn eq(&self, other: &Self) -> bool {
        unsafe { bindings::qobject_is_equal(self.0.get(), other.0.get()) }
    }
}

impl QObject {
    /// Construct a [`QObject`] from a C `QObjectBase` pointer.
    /// The caller cedes its reference to the returned struct.
    ///
    /// # Safety
    ///
    /// The `QObjectBase` must not be changed from C code while
    /// the Rust `QObject` lives
    const unsafe fn from_base(p: *const bindings::QObjectBase_) -> Self {
        QObject(unsafe { &*p.cast() })
    }

    /// Construct a [`QObject`] from a C `QObject` pointer.
    /// The caller cedes its reference to the returned struct.
    ///
    /// # Safety
    ///
    /// The `QObject` must not be changed from C code while
    /// the Rust `QObject` lives
    pub const unsafe fn from_raw(p: *const bindings::QObject) -> Self {
        QObject(unsafe { &*p.cast() })
    }

    /// Obtain a raw C pointer from a reference. `self` is consumed
    /// and the C `QObject` pointer is leaked.
    pub fn into_raw(self) -> *mut bindings::QObject {
        let src = ManuallyDrop::new(self);
        src.0.get()
    }

    /// Construct a [`QObject`] from a C `QObject` pointer.
    /// The caller *does not* cede its reference to the returned struct.
    ///
    /// # Safety
    ///
    /// The `QObjectBase` must not be changed from C code while
    /// the Rust `QObject` lives
    unsafe fn cloned_from_base(p: *const bindings::QObjectBase_) -> Self {
        let orig = unsafe { ManuallyDrop::new(QObject::from_base(p)) };
        (*orig).clone()
    }

    /// Construct a [`QObject`] from a C `QObject` pointer.
    /// The caller *does not* cede its reference to the returned struct.
    ///
    /// # Safety
    ///
    /// The `QObject` must not be changed from C code while
    /// the Rust `QObject` lives
    pub unsafe fn cloned_from_raw(p: *const bindings::QObject) -> Self {
        let orig = unsafe { ManuallyDrop::new(QObject::from_raw(p)) };
        (*orig).clone()
    }

    fn refcnt(&self) -> &AtomicUsize {
        assert_field_type!(bindings::QObjectBase_, refcnt, usize);
        let qobj = self.0.get();
        unsafe { AtomicUsize::from_ptr(addr_of_mut!((*qobj).base.refcnt)) }
    }

    pub fn to_json(&self) -> String {
        let qobj = self.0.get();
        unsafe {
            let json = bindings::qobject_to_json(qobj);
            glib_sys::g_string_free(json, glib_sys::GFALSE).into_native()
        }
    }

    pub fn from_json(json: &str) -> std::result::Result<Self, crate::Error> {
        let c_json = std::ffi::CString::new(json)?;
        unsafe {
            crate::Error::with_errp(|errp| bindings::qobject_from_json(c_json.as_ptr(), errp))
                .map(|qobj| QObject::from_raw(qobj))
        }
    }
}

impl From<()> for QObject {
    fn from(_null: ()) -> Self {
        unsafe { QObject::cloned_from_base(addr_of!(bindings::qnull_.base)) }
    }
}

impl<T> From<Option<T>> for QObject
where
    QObject: From<T>,
{
    fn from(o: Option<T>) -> Self {
        o.map_or_else(|| ().into(), Into::into)
    }
}

impl From<bool> for QObject {
    fn from(b: bool) -> Self {
        let qobj = unsafe { &*bindings::qbool_from_bool(b) };
        unsafe { QObject::from_base(addr_of!(qobj.base)) }
    }
}

macro_rules! from_int {
    ($t:ty) => {
        impl From<$t> for QObject {
            fn from(n: $t) -> Self {
                let qobj = unsafe { &*bindings::qnum_from_int(n.into()) };
                unsafe { QObject::from_base(addr_of!(qobj.base)) }
            }
        }
    };
}

from_int!(i8);
from_int!(i16);
from_int!(i32);
from_int!(i64);

macro_rules! from_uint {
    ($t:ty) => {
        impl From<$t> for QObject {
            fn from(n: $t) -> Self {
                let qobj = unsafe { &*bindings::qnum_from_uint(n.into()) };
                unsafe { QObject::from_base(addr_of!(qobj.base)) }
            }
        }
    };
}

from_uint!(u8);
from_uint!(u16);
from_uint!(u32);
from_uint!(u64);

macro_rules! from_double {
    ($t:ty) => {
        impl From<$t> for QObject {
            fn from(n: $t) -> Self {
                let qobj = unsafe { &*bindings::qnum_from_double(n.into()) };
                unsafe { QObject::from_base(addr_of!(qobj.base)) }
            }
        }
    };
}

from_double!(f32);
from_double!(f64);

impl From<CString> for QObject {
    fn from(s: CString) -> Self {
        let qobj = unsafe { &*bindings::qstring_from_str(s.as_ptr()) };
        unsafe { QObject::from_base(addr_of!(qobj.base)) }
    }
}

impl<A> FromIterator<A> for QObject
where
    Self: From<A>,
{
    fn from_iter<I: IntoIterator<Item = A>>(it: I) -> Self {
        let qlist = unsafe { &mut *bindings::qlist_new() };
        for elem in it {
            let elem: QObject = elem.into();
            let elem = elem.into_raw();
            unsafe {
                bindings::qlist_append_obj(qlist, elem);
            }
        }
        unsafe { QObject::from_base(addr_of!(qlist.base)) }
    }
}

impl<A> FromIterator<(CString, A)> for QObject
where
    Self: From<A>,
{
    fn from_iter<I: IntoIterator<Item = (CString, A)>>(it: I) -> Self {
        let qdict = unsafe { &mut *bindings::qdict_new() };
        for (key, val) in it {
            let val: QObject = val.into();
            let val = val.into_raw();
            unsafe {
                bindings::qdict_put_obj(qdict, key.as_ptr().cast::<c_char>(), val);
            }
        }
        unsafe { QObject::from_base(addr_of!(qdict.base)) }
    }
}

impl Clone for QObject {
    fn clone(&self) -> Self {
        self.refcnt().fetch_add(1, Ordering::Acquire);
        QObject(self.0)
    }
}

impl Drop for QObject {
    fn drop(&mut self) {
        if self.refcnt().fetch_sub(1, Ordering::Release) == 1 {
            unsafe {
                bindings::qobject_destroy(self.0.get());
            }
        }
    }
}

impl Display for QObject {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // replace with a plain serializer?
        match_qobject! { (self) =>
            () => write!(f, "QNull"),
            bool(b) => write!(f, "QBool({})", if b { "true" } else { "false" }),
            i64(n) => write!(f, "QNumI64({})", n),
            u64(n) => write!(f, "QNumU64({})", n),
            f64(n) => write!(f, "QNumDouble({})", n),
            CStr(s) => write!(f, "QString({})", s.to_str().unwrap_or("bad CStr")),
            QList(_) => write!(f, "QList"),
            QDict(_) => write!(f, "QDict"),
        }
    }
}

impl Debug for QObject {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let val = self.to_string();
        f.debug_struct("QObject")
            .field("ptr", &self.0.get())
            .field("refcnt()", &self.refcnt())
            .field("to_string()", &val)
            .finish()
    }
}

macro_rules! match_qobject {
    (@internal ($qobj:expr) =>
        $(() => $unit:expr,)?
        $(bool($boolvar:tt) => $bool:expr,)?
        $(i64($i64var:tt) => $i64:expr,)?
        $(u64($u64var:tt) => $u64:expr,)?
        $(f64($f64var:tt) => $f64:expr,)?
        $(CStr($cstrvar:tt) => $cstr:expr,)?
        $(QList($qlistvar:tt) => $qlist:expr,)?
        $(QDict($qdictvar:tt) => $qdict:expr,)?
        $(_ => $other:expr,)?
    ) => {
        loop {
            let qobj_ = $qobj.0.get();
            match unsafe { &* qobj_ }.base.type_ {
                $($crate::bindings::QTYPE_QNULL => break $unit,)?
                $($crate::bindings::QTYPE_QBOOL => break {
                    let qbool__: *mut $crate::bindings::QBool = qobj_.cast();
                    let $boolvar = unsafe { (&*qbool__).value };
                    $bool
                },)?
                $crate::bindings::QTYPE_QNUM => {
                    let qnum__: *mut $crate::bindings::QNum = qobj_.cast();
                    let qnum__ = unsafe { &*qnum__ };
                    match qnum__.kind {
                        $crate::bindings::QNUM_I64 |
                        $crate::bindings::QNUM_U64 |
                        $crate::bindings::QNUM_DOUBLE => {}
                        _ => {
                            panic!("unreachable");
                        }
                    }

                    match qnum__.kind {
                        $($crate::bindings::QNUM_I64 => break {
                            let $i64var = unsafe { qnum__.u.i64_ };
                            $i64
                        },)?
                        $($crate::bindings::QNUM_U64 => break {
                            let $u64var = unsafe { qnum__.u.u64_ };
                            $u64
                        },)?
                        $($crate::bindings::QNUM_DOUBLE => break {
                            let $f64var = unsafe { qnum__.u.dbl };
                            $f64
                        },)?
                        _ => {}
                    }
                },
                $($crate::bindings::QTYPE_QSTRING => break {
                    let qstring__: *mut $crate::bindings::QString = qobj_.cast();
                    let $cstrvar = unsafe { ::core::ffi::CStr::from_ptr((&*qstring__).string) };
                    $cstr
                },)?
                $($crate::bindings::QTYPE_QLIST => break {
                    let qlist__: *mut $crate::bindings::QList = qobj_.cast();
                    let $qlistvar = unsafe { &*qlist__ };
                    $qlist
                },)?
                $($crate::bindings::QTYPE_QDICT => break {
                    let qdict__: *mut $crate::bindings::QDict = qobj_.cast();
                    let $qdictvar = unsafe { &*qdict__ };
                    $qdict
                },)?
                _ => ()
            };
            $(break $other;)?
            #[allow(unreachable_code)]
            {
                panic!("unreachable");
            }
        }
    };

    // first cleanup the syntax a bit, checking that there's at least
    // one pattern and always adding a trailing comma
    (($qobj:expr) =>
        $($type:tt$(($val:tt))? => $code:expr ),+ $(,)?) => {
            match_qobject!(@internal ($qobj) =>
                $($type $(($val))? => $code,)+)
    };
}
use match_qobject;
