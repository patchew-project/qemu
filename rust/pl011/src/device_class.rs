use core::{mem::MaybeUninit, ptr::NonNull};
use std::sync::OnceLock;

use crate::{
    device::{PL011State, VMSTATE_PL011},
    generated::*,
};

#[no_mangle]
pub unsafe extern "C" fn pl011_class_init(klass: *mut ObjectClass, _: *mut core::ffi::c_void) {
    let mut dc = NonNull::new(klass.cast::<DeviceClass>()).unwrap();
    dc.as_mut().realize = Some(pl011_realize);
    dc.as_mut().reset = Some(pl011_reset);
    dc.as_mut().vmsd = &VMSTATE_PL011;
    _ = PL011_PROPERTIES.get_or_init(make_pl011_properties);
    device_class_set_props(
        dc.as_mut(),
        PL011_PROPERTIES.get_mut().unwrap().as_mut_ptr(),
    );
}

#[macro_export]
macro_rules! define_property {
    ($name:expr, $state:ty, $field:expr, $prop:expr, $type:expr, default = $defval:expr) => {
        $crate::generated::Property {
            name: $name,
            info: $prop,
            offset: ::core::mem::offset_of!($state, $field) as _,
            bitnr: 0,
            bitmask: 0,
            set_default: true,
            defval: $crate::generated::Property__bindgen_ty_1 { u: $defval.into() },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
    ($name:expr, $state:ty, $field:expr, $prop:expr, $type:expr) => {
        $crate::generated::Property {
            name: $name,
            info: $prop,
            offset: ::core::mem::offset_of!($state, $field) as _,
            bitnr: 0,
            bitmask: 0,
            set_default: false,
            defval: $crate::generated::Property__bindgen_ty_1 { i: 0 },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
}

#[no_mangle]
pub static mut PL011_PROPERTIES: OnceLock<[Property; 3]> = OnceLock::new();

unsafe impl Send for Property {}
unsafe impl Sync for Property {}

#[no_mangle]
fn make_pl011_properties() -> [Property; 3] {
    [
        define_property!(
            c"chardev".as_ptr(),
            PL011State,
            char_backend,
            unsafe { &qdev_prop_chr },
            CharBackend
        ),
        define_property!(
            c"migrate-clk".as_ptr(),
            PL011State,
            migrate_clock,
            unsafe { &qdev_prop_bool },
            bool
        ),
        unsafe { MaybeUninit::<Property>::zeroed().assume_init() },
    ]
}

#[no_mangle]
pub unsafe extern "C" fn pl011_realize(dev: *mut DeviceState, _errp: *mut *mut Error) {
    assert!(!dev.is_null());
    let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
    state.as_mut().realize();
}

#[no_mangle]
pub unsafe extern "C" fn pl011_reset(dev: *mut DeviceState) {
    assert!(!dev.is_null());
    let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
    state.as_mut().reset();
}
