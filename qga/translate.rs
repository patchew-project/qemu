// largely adapted from glib-rs
// we don't depend on glib-rs as this brings a lot more code that we may not need
// and also because there are issues with the conversion traits for our sys::*mut.
use libc::{c_char, size_t};
use std::ffi::{CStr, CString};
use std::ptr;

use crate::qemu_sys;

pub trait Ptr: Copy + 'static {
    fn is_null(&self) -> bool;
    fn from<X>(ptr: *mut X) -> Self;
    fn to<X>(self) -> *mut X;
}

impl<T: 'static> Ptr for *const T {
    #[inline]
    fn is_null(&self) -> bool {
        (*self).is_null()
    }

    #[inline]
    fn from<X>(ptr: *mut X) -> *const T {
        ptr as *const T
    }

    #[inline]
    fn to<X>(self) -> *mut X {
        self as *mut X
    }
}

impl<T: 'static> Ptr for *mut T {
    #[inline]
    fn is_null(&self) -> bool {
        (*self).is_null()
    }

    #[inline]
    fn from<X>(ptr: *mut X) -> *mut T {
        ptr as *mut T
    }

    #[inline]
    fn to<X>(self) -> *mut X {
        self as *mut X
    }
}

/// Provides the default pointer type to be used in some container conversions.
///
/// It's `*mut c_char` for `String`, `*mut GtkButton` for `gtk::Button`, etc.
pub trait QemuPtrDefault {
    type QemuType: Ptr;
}

impl QemuPtrDefault for String {
    type QemuType = *mut c_char;
}

pub struct Stash<'a, P: Copy, T: ?Sized + ToQemuPtr<'a, P>>(
    pub P,
    pub <T as ToQemuPtr<'a, P>>::Storage,
);

/// Translate to a pointer.
pub trait ToQemuPtr<'a, P: Copy> {
    type Storage;

    /// Transfer: none.
    ///
    /// The pointer in the `Stash` is only valid for the lifetime of the `Stash`.
    fn to_qemu_none(&'a self) -> Stash<'a, P, Self>;

    /// Transfer: full.
    ///
    /// We transfer the ownership to the foreign library.
    fn to_qemu_full(&self) -> P {
        unimplemented!();
    }
}

impl<'a, P: Ptr, T: ToQemuPtr<'a, P>> ToQemuPtr<'a, P> for Option<T> {
    type Storage = Option<<T as ToQemuPtr<'a, P>>::Storage>;

    #[inline]
    fn to_qemu_none(&'a self) -> Stash<'a, P, Option<T>> {
        self.as_ref()
            .map_or(Stash(Ptr::from::<()>(ptr::null_mut()), None), |s| {
                let s = s.to_qemu_none();
                Stash(s.0, Some(s.1))
            })
    }

    #[inline]
    fn to_qemu_full(&self) -> P {
        self.as_ref()
            .map_or(Ptr::from::<()>(ptr::null_mut()), ToQemuPtr::to_qemu_full)
    }
}

impl<'a> ToQemuPtr<'a, *mut c_char> for String {
    type Storage = CString;

    #[inline]
    fn to_qemu_none(&self) -> Stash<'a, *mut c_char, String> {
        let tmp = CString::new(&self[..])
            .expect("String::ToQemuPtr<*mut c_char>: unexpected '\0' character");
        Stash(tmp.as_ptr() as *mut c_char, tmp)
    }

    #[inline]
    fn to_qemu_full(&self) -> *mut c_char {
        unsafe { qemu_sys::g_strndup(self.as_ptr() as *const c_char, self.len() as size_t) }
    }
}

pub trait FromQemuPtrNone<P: Ptr>: Sized {
    unsafe fn from_qemu_none(ptr: P) -> Self;
}

pub trait FromQemuPtrFull<P: Ptr>: Sized {
    unsafe fn from_qemu_full(ptr: P) -> Self;
}

#[inline]
pub unsafe fn from_qemu_none<P: Ptr, T: FromQemuPtrNone<P>>(ptr: P) -> T {
    FromQemuPtrNone::from_qemu_none(ptr)
}

#[inline]
pub unsafe fn from_qemu_full<P: Ptr, T: FromQemuPtrFull<P>>(ptr: P) -> T {
    FromQemuPtrFull::from_qemu_full(ptr)
}

impl<P: Ptr, T: FromQemuPtrNone<P>> FromQemuPtrNone<P> for Option<T> {
    #[inline]
    unsafe fn from_qemu_none(ptr: P) -> Option<T> {
        if ptr.is_null() {
            None
        } else {
            Some(from_qemu_none(ptr))
        }
    }
}

impl<P: Ptr, T: FromQemuPtrFull<P>> FromQemuPtrFull<P> for Option<T> {
    #[inline]
    unsafe fn from_qemu_full(ptr: P) -> Option<T> {
        if ptr.is_null() {
            None
        } else {
            Some(from_qemu_full(ptr))
        }
    }
}

impl FromQemuPtrNone<*const c_char> for String {
    #[inline]
    unsafe fn from_qemu_none(ptr: *const c_char) -> Self {
        assert!(!ptr.is_null());
        String::from_utf8_lossy(CStr::from_ptr(ptr).to_bytes()).into_owned()
    }
}

impl FromQemuPtrFull<*mut c_char> for String {
    #[inline]
    unsafe fn from_qemu_full(ptr: *mut c_char) -> Self {
        let res = from_qemu_none(ptr as *const _);
        qemu_sys::g_free(ptr as *mut _);
        res
    }
}
