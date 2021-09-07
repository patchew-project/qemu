#![allow(dead_code)]

use common::libc;

include!(concat!(
    env!("MESON_BUILD_ROOT"),
    "/tests/test-qapi-ffi-types.rs"
));
