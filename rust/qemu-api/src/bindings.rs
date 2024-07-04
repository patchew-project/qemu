// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

#[cfg(MESON_BINDINGS_RS)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(not(MESON_BINDINGS_RS))]
include!("bindings.rs.inc");
