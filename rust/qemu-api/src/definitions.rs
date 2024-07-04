// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

//! Definitions required by QEMU when registering a device.

use crate::bindings::*;

unsafe impl Sync for TypeInfo {}
unsafe impl Sync for VMStateDescription {}

#[macro_export]
macro_rules! module_init {
    ($func:expr, $type:expr) => {
        #[used]
        #[cfg_attr(target_os = "linux", link_section = ".ctors")]
        #[cfg_attr(target_os = "macos", link_section = "__DATA,__mod_init_func")]
        #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
        pub static LOAD_MODULE: extern "C" fn() = {
            assert!($type < $crate::bindings::module_init_type_MODULE_INIT_MAX);

            extern "C" fn __load() {
                // ::std::panic::set_hook(::std::boxed::Box::new(|_| {}));

                unsafe {
                    $crate::bindings::register_module_init(Some($func), $type);
                }
            }

            __load
        };
    };
    (qom: $func:ident => $body:block) => {
        // NOTE: To have custom identifiers for the ctor func we need to either supply
        // them directly as a macro argument or create them with a proc macro.
        #[used]
        #[cfg_attr(target_os = "linux", link_section = ".ctors")]
        #[cfg_attr(target_os = "macos", link_section = "__DATA,__mod_init_func")]
        #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
        pub static LOAD_MODULE: extern "C" fn() = {
            extern "C" fn __load() {
                // ::std::panic::set_hook(::std::boxed::Box::new(|_| {}));
                #[no_mangle]
                unsafe extern "C" fn $func() {
                    $body
                }

                unsafe {
                    $crate::bindings::register_module_init(
                        Some($func),
                        $crate::bindings::module_init_type_MODULE_INIT_QOM,
                    );
                }
            }

            __load
        };
    };
}

#[macro_export]
macro_rules! type_info {
    ($(#[$outer:meta])*
     $name:ident: $t:ty,
     $(name: $tname:expr,)*
     $(parent: $pname:expr,)*
     $(instance_init: $ii_fn:expr,)*
     $(instance_post_init: $ipi_fn:expr,)*
     $(instance_finalize: $if_fn:expr,)*
     $(abstract_: $a_val:expr,)*
     $(class_init: $ci_fn:expr,)*
     $(class_base_init: $cbi_fn:expr,)*
    ) => {
        #[used]
        $(#[$outer])*
        pub static $name: $crate::bindings::TypeInfo = $crate::bindings::TypeInfo {
                $(name: {
                #[used]
                static TYPE_NAME: &::core::ffi::CStr = $tname;
                $tname.as_ptr()
            },)*
            $(parent: {
                #[used]
                static PARENT_TYPE_NAME: &::core::ffi::CStr = $pname;
                $pname.as_ptr()
            },)*
            instance_size: ::core::mem::size_of::<$t>(),
            instance_align: ::core::mem::align_of::<$t>(),
            $(
                instance_init: $ii_fn,
            )*
            $(
                instance_post_init: $ipi_fn,
            )*
            $(
                instance_finalize: $if_fn,
            )*
            $(
                abstract_: $a_val,
            )*
            class_size: 0,
            $(
                class_init: $ci_fn,
            )*
            $(
                class_base_init: $cbi_fn,
            )*
            class_data: core::ptr::null_mut(),
            interfaces: core::ptr::null_mut(),
            ..unsafe { MaybeUninit::<$crate::bindings::TypeInfo>::zeroed().assume_init() }
        };
    }
}
