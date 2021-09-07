#![allow(dead_code)]

use common::libc;

include!(concat!(
    env!("MESON_BUILD_ROOT"),
    "/qga/qga-qapi-ffi-types.rs"
));
