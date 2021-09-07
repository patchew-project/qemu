#![allow(dead_code)]
use common::*;

new_ptr!();

include!(concat!(env!("MESON_BUILD_ROOT"), "/qga/qga-qapi-types.rs"));
