// largely adapted from glib-rs
// we don't depend on glib-rs as this brings a lot more code that we may not need
// and also because there are issues with the conversion traits for our sys::*mut.
use libc::{c_char, size_t};
use std::ffi::{CStr, CString};
use std::ptr;

use crate::sys;

/// A pointer.
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

/// Macro for NewPtr.
///
/// A macro to declare a newtype for pointers, to workaround that *T are not
/// defined in our binding crates, and allow foreign traits implementations.
/// (this is used by qapi-gen bindings)
#[allow(unused_macros)]
#[macro_export]
macro_rules! new_ptr {
    () => {
        #[derive(Copy, Clone)]
        pub struct NewPtr<P: Ptr>(pub P);

        impl<P: Ptr> Ptr for NewPtr<P> {
            #[inline]
            fn is_null(&self) -> bool {
                self.0.is_null()
            }

            #[inline]
            fn from<X>(ptr: *mut X) -> Self {
                NewPtr(P::from(ptr))
            }

            #[inline]
            fn to<X>(self) -> *mut X {
                self.0.to()
            }
        }
    };
}

/// Provides the default pointer type to be used in some container conversions.
///
/// It's `*mut c_char` for `String`, `*mut sys::GuestInfo` for `GuestInfo`...
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

    /// The pointer in the `Stash` is only valid for the lifetime of the `Stash`.
    fn to_qemu_none(&'a self) -> Stash<'a, P, Self>;

    /// Transfer the ownership to the ffi.
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
        unsafe { sys::g_strndup(self.as_ptr() as *const c_char, self.len() as size_t) }
    }
}

/// Translate from a pointer type, without taking ownership.
pub trait FromQemuPtrNone<P: Ptr>: Sized {
    /// # Safety
    ///
    /// `ptr` must be a valid pointer. It is not referenced after the call.
    unsafe fn from_qemu_none(ptr: P) -> Self;
}

/// Translate from a pointer type, taking ownership.
pub trait FromQemuPtrFull<P: Ptr>: Sized {
    /// # Safety
    ///
    /// `ptr` must be a valid pointer. Ownership is transferred.
    unsafe fn from_qemu_full(ptr: P) -> Self;
}

/// See [`FromQemuPtrNone`](trait.FromQemuPtrNone.html).
#[inline]
#[allow(clippy::missing_safety_doc)]
pub unsafe fn from_qemu_none<P: Ptr, T: FromQemuPtrNone<P>>(ptr: P) -> T {
    FromQemuPtrNone::from_qemu_none(ptr)
}

/// See [`FromQemuPtrFull`](trait.FromQemuPtrFull.html).
#[inline]
#[allow(clippy::missing_safety_doc)]
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
        sys::g_free(ptr as *mut _);
        res
    }
}

/// A macro to help the implementation of `Vec<T> -> P` translations.
#[allow(unused_macros)]
#[macro_export]
macro_rules! vec_to_qemu {
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

        impl<'a> ToQemuPtr<'a, NewPtr<*mut qapi_sys::$sys>> for Vec<$rs> {
            type Storage = (
                Option<$sys>,
                Vec<Stash<'a, <$rs as QemuPtrDefault>::QemuType, $rs>>,
            );

            #[inline]
            fn to_qemu_none(&self) -> Stash<NewPtr<*mut qapi_sys::$sys>, Self> {
                let stash_vec: Vec<_> = self.iter().rev().map(ToQemuPtr::to_qemu_none).collect();
                let mut list: *mut qapi_sys::$sys = std::ptr::null_mut();
                for stash in &stash_vec {
                    let b = Box::new(qapi_sys::$sys {
                        next: list,
                        value: Ptr::to(stash.0),
                    });
                    list = Box::into_raw(b);
                }
                Stash(NewPtr(list), (Some($sys(list)), stash_vec))
            }

            #[inline]
            fn to_qemu_full(&self) -> NewPtr<*mut qapi_sys::$sys> {
                let v: Vec<_> = self.iter().rev().map(ToQemuPtr::to_qemu_full).collect();
                let mut list: *mut qapi_sys::$sys = std::ptr::null_mut();
                unsafe {
                    for val in v {
                        let l = sys::g_malloc0(std::mem::size_of::<qapi_sys::$sys>())
                            as *mut qapi_sys::$sys;
                        (*l).next = list;
                        (*l).value = val;
                        list = l;
                    }
                }
                NewPtr(list)
            }
        }
    };
}

/// A macro to help the implementation of `P -> Vec<T>` translations.
#[allow(unused_macros)]
#[macro_export]
macro_rules! vec_from_qemu {
    ($rs:ident, $sys:ident, $free_sys:ident) => {
        impl FromQemuPtrFull<NewPtr<*mut qapi_sys::$sys>> for Vec<$rs> {
            #[inline]
            unsafe fn from_qemu_full(sys: NewPtr<*mut qapi_sys::$sys>) -> Self {
                let ret = from_qemu_none(NewPtr(sys.0 as *const _));
                qapi_sys::$free_sys(sys.0);
                ret
            }
        }

        impl FromQemuPtrNone<NewPtr<*const qapi_sys::$sys>> for Vec<$rs> {
            #[inline]
            unsafe fn from_qemu_none(sys: NewPtr<*const qapi_sys::$sys>) -> Self {
                let mut ret = vec![];
                let mut it = sys.0;
                while !it.is_null() {
                    let e = &*it;
                    ret.push(from_qemu_none(e.value as *const _));
                    it = e.next;
                }
                ret
            }
        }

        impl From<NewPtr<*mut qapi_sys::$sys>> for *mut qapi_sys::$sys {
            fn from(p: NewPtr<*mut qapi_sys::$sys>) -> Self {
                p.0
            }
        }
    };
}
