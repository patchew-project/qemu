// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#![cfg_attr(not(MESON), doc = include_str!("../README.md"))]
#![deny(clippy::missing_const_for_fn)]

#[rustfmt::skip]
pub mod bindings;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;
