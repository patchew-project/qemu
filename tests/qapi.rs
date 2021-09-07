#![allow(dead_code)]
#![allow(non_camel_case_types)]

use common::*;

new_ptr!();

include!(concat!(
    env!("MESON_BUILD_ROOT"),
    "/tests/test-qapi-types.rs"
));
