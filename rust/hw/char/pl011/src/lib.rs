// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! PL011 QEMU Device Model
//!
//! This library implements a device model for the PrimeCell® UART (PL011)
//! device in QEMU.
//!
//! # Library crate
//!
//! See [`PL011State`](crate::device::PL011State) for the device model type and
//! the [`registers`] module for register types.

mod device;
mod registers;

pub use device::pl011_create;

pub const TYPE_PL011: &::std::ffi::CStr = c"pl011";
pub const TYPE_PL011_LUMINARY: &::std::ffi::CStr = c"pl011_luminary";

#[qemu_api_macros::trace_events]
pub mod trace_events {
    fn pl011_irq_state(level: u32) {
        "irq state {level}"
    }

    fn pl011_read(
        addr: qemu_api::memory::hwaddr,
        value: u32,
        regname: crate::registers::RegisterOffset,
    ) {
        "addr {addr:#x} value {value:#x} reg {regname:?}"
    }

    fn pl011_read_fifo(rx_fifo_used: u32, rx_fifo_depth: u32) {
        "RX FIFO read, used {rx_fifo_used}/{rx_fifo_depth}"
    }

    fn pl011_write(
        addr: qemu_api::memory::hwaddr,
        value: u64,
        regname: crate::registers::RegisterOffset,
    ) {
        "addr {addr:#x} value {value:#x} reg {regname:?}"
    }

    fn pl011_can_receive(lcr: u32, rx_fifo_used: u32, rx_fifo_depth: u32, rx_fifo_available: u32) {
        "LCR {lcr:#x}, RX FIFO used {rx_fifo_used}/{rx_fifo_depth}, can_receive {rx_fifo_available} chars"
    }

    fn pl011_fifo_rx_put(c: u32, read_count: u32, rx_fifo_depth: u32) {
        "RX FIFO push char [{c:#x}] {read_count}/{rx_fifo_depth} depth used"
    }

    fn pl011_fifo_rx_full() {
        "RX FIFO now full, RXFF set"
    }

    fn pl011_receive(size: usize) {
        "recv {size} chars"
    }
}
