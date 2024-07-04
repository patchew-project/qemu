// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

#![doc = include_str!("../README.md")]

// FIXME: remove improper_ctypes
#[allow(
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals
)]
#[allow(
    clippy::missing_const_for_fn,
    clippy::useless_transmute,
    clippy::too_many_arguments,
    clippy::approx_constant,
    clippy::use_self,
    clippy::cast_lossless,
)]
#[rustfmt::skip]
pub mod bindings;

pub mod definitions;
pub mod device_class;

#[cfg(test)]
mod tests;
