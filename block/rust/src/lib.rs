/* Rust cannot not complain about unused public interfaces, which is rather
 * annoying */
#![allow(dead_code)]

extern crate core;
extern crate libc;

#[macro_use]
mod interface;
