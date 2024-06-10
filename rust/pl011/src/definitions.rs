//! Definitions required by QEMU when registering the device.

use core::{ffi::CStr, mem::MaybeUninit, ptr::NonNull};

use crate::{device::PL011State, device_class::pl011_class_init, generated::*};

pub const TYPE_PL011: &CStr = unsafe { CStr::from_bytes_with_nul_unchecked(b"x-pl011-rust\0") };

pub const PL011_ARM_INFO: TypeInfo = TypeInfo {
    name: TYPE_PL011.as_ptr(),
    parent: TYPE_SYS_BUS_DEVICE.as_ptr(),
    instance_size: core::mem::size_of::<PL011State>(),
    instance_align: core::mem::align_of::<PL011State>(),
    instance_init: Some(pl011_init),
    instance_post_init: None,
    instance_finalize: None,
    abstract_: false,
    class_size: 0,
    class_init: Some(pl011_class_init),
    class_base_init: None,
    class_data: core::ptr::null_mut(),
    interfaces: core::ptr::null_mut(),
};

pub const VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: PL011_ARM_INFO.name,
    unmigratable: true,
    ..unsafe { MaybeUninit::<VMStateDescription>::zeroed().assume_init() }
};
//version_id : 2,
//minimum_version_id : 2,
//post_load : pl011_post_load,
//fields = (const VMStateField[]) {
//    VMSTATE_UINT32(readbuff, PL011State),
//    VMSTATE_UINT32(flags, PL011State),
//    VMSTATE_UINT32(lcr, PL011State),
//    VMSTATE_UINT32(rsr, PL011State),
//    VMSTATE_UINT32(cr, PL011State),
//    VMSTATE_UINT32(dmacr, PL011State),
//    VMSTATE_UINT32(int_enabled, PL011State),
//    VMSTATE_UINT32(int_level, PL011State),
//    VMSTATE_UINT32_ARRAY(read_fifo, PL011State, PL011_FIFO_DEPTH),
//    VMSTATE_UINT32(ilpr, PL011State),
//    VMSTATE_UINT32(ibrd, PL011State),
//    VMSTATE_UINT32(fbrd, PL011State),
//    VMSTATE_UINT32(ifl, PL011State),
//    VMSTATE_INT32(read_pos, PL011State),
//    VMSTATE_INT32(read_count, PL011State),
//    VMSTATE_INT32(read_trigger, PL011State),
//    VMSTATE_END_OF_LIST()
//},
//.subsections = (const VMStateDescription * const []) {
//    &vmstate_pl011_clock,
//    NULL
//}

#[no_mangle]
pub unsafe extern "C" fn pl011_init(obj: *mut Object) {
    assert!(!obj.is_null());
    let mut state = NonNull::new_unchecked(obj.cast::<PL011State>());
    state.as_mut().init();
}

use crate::generated::module_init_type_MODULE_INIT_QOM;

#[macro_export]
macro_rules! module_init {
    ($func:expr, $type:expr) => {
        #[cfg_attr(target_os = "linux", link_section = ".ctors")]
        #[cfg_attr(target_os = "macos", link_section = "__DATA,__mod_init_func")]
        #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
        pub static LOAD_MODULE: extern "C" fn() = {
            assert!($type < $crate::generated::module_init_type_MODULE_INIT_MAX);

            extern "C" fn __load() {
                // ::std::panic::set_hook(::std::boxed::Box::new(|_| {}));

                unsafe {
                    $crate::generated::register_module_init(Some($func), $type);
                }
            }

            __load
        };
    };
}

#[no_mangle]
unsafe extern "C" fn register_type() {
    crate::generated::type_register_static(&PL011_ARM_INFO);
}

module_init! {
    register_type, module_init_type_MODULE_INIT_QOM
}
