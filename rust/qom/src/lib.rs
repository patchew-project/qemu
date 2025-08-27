// SPDX-License-Identifier: GPL-2.0-or-later

pub use qemu_macros::Object;

pub mod bindings;

pub mod prelude;

mod qom;
pub use qom::*;
