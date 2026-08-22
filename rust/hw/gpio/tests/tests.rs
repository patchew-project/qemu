// Copyright 2026, Jack Wang
// Author(s): Jack Wang <163wangjack@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use stm32f1xx_gpio::gpio::{
    GpioRegisters, PinUpdate, REG_BRR, REG_BSRR, REG_CRH, REG_CRL, REG_IDR, REG_LCKR, REG_ODR,
    RESET_CRH, RESET_CRL,
};

fn regs() -> GpioRegisters {
    bql::start_test();
    GpioRegisters::new()
}

const CFG_INPUT_ANALOG: u32 = 0x0;
const CFG_INPUT_FLOATING: u32 = 0x4;
const CFG_INPUT_PULL: u32 = 0x8;
const CFG_OUTPUT_PP: u32 = 0x1;
const CFG_OUTPUT_OD: u32 = 0x5;
const CFG_AF_PP: u32 = 0x9;

fn set_config(r: &GpioRegisters, pin: usize, cfg: u32) {
    let reg = if pin < 8 { REG_CRL } else { REG_CRH };
    let shift = ((pin % 8) * 4) as u32;
    let old = r.read(reg);
    r.write(reg, (old & !(0xF << shift)) | (cfg << shift));
}

fn set_odr_bit(r: &GpioRegisters, pin: usize, value: u32) {
    let old = r.read(REG_ODR);
    r.write(REG_ODR, (old & !(1 << pin)) | (value << pin));
}

fn pin_changed_to(upd: PinUpdate, pin: usize, level: bool) -> bool {
    (upd.mask & (1 << pin)) != 0 && (((upd.levels >> pin) & 1) != 0) == level
}

#[test]
fn test_reset_values() {
    let r = regs();
    assert_eq!(r.read(REG_CRL), RESET_CRL);
    assert_eq!(r.read(REG_CRH), RESET_CRH);
    assert_eq!(r.read(REG_IDR), 0);
    assert_eq!(r.read(REG_ODR), 0);
}

#[test]
fn test_reset_restores() {
    let r = regs();
    r.write(REG_CRL, 0xDEAD_BEEF);
    r.write(REG_ODR, 0x0000_FFFF);
    assert_ne!(r.read(REG_CRL), RESET_CRL);

    r.reset();

    assert_eq!(r.read(REG_CRL), RESET_CRL);
    assert_eq!(r.read(REG_CRH), RESET_CRH);
    assert_eq!(r.read(REG_IDR), 0);
    assert_eq!(r.read(REG_ODR), 0);
}

#[test]
fn test_output_mode() {
    let r = regs();
    let pin = 5;

    let idle = set_odr_and_report(&r, pin, 1);
    assert_eq!(r.read(REG_IDR), 0);
    assert_eq!(idle.mask & (1 << pin), 0);

    let old = r.read(REG_CRL);
    let to_output = r.write(
        REG_CRL,
        (old & !(0xF << (pin * 4))) | (CFG_OUTPUT_PP << (pin * 4)),
    );
    assert_eq!(r.read(REG_IDR), 1 << pin);
    assert!(pin_changed_to(to_output, pin, true));

    let to_low = set_odr_and_report(&r, pin, 0);
    assert_eq!(r.read(REG_IDR), 0);
    assert!(pin_changed_to(to_low, pin, false));
}

fn set_odr_and_report(r: &GpioRegisters, pin: usize, value: u32) -> PinUpdate {
    let old = r.read(REG_ODR);
    r.write(REG_ODR, (old & !(1 << pin)) | (value << pin))
}

#[test]
fn test_input_mode() {
    let r = regs();
    let pin = 6usize;

    set_config(&r, pin, CFG_INPUT_FLOATING);

    let drive_high = r.gpio_set(pin, 1);
    assert_eq!(r.read(REG_IDR), 1 << pin);
    assert!(pin_changed_to(drive_high, pin, true));

    let drive_low = r.gpio_set(pin, 0);
    assert_eq!(r.read(REG_IDR), 0);
    assert!(pin_changed_to(drive_low, pin, false));
}

#[test]
fn test_pull_up_down() {
    let r = regs();
    let pin = 0;

    set_odr_bit(&r, pin, 1);
    set_config(&r, pin, CFG_INPUT_PULL);
    assert_eq!(r.read(REG_IDR), 1 << pin);

    set_odr_bit(&r, pin, 0);
    assert_eq!(r.read(REG_IDR), 0);
}

#[test]
fn test_bsrr_brr() {
    let r = regs();
    let pin = 1;

    r.write(REG_BSRR, 1 << pin);
    assert_eq!(r.read(REG_ODR), 1 << pin);

    r.write(REG_BSRR, 1 << (pin + 16));
    assert_eq!(r.read(REG_ODR), 0);

    r.write(REG_BSRR, 1 << pin);
    r.write(REG_BRR, 1 << pin);
    assert_eq!(r.read(REG_ODR), 0);

    // Set has priority over reset within one BSRR write.
    r.write(REG_BSRR, (1 << pin) | (1 << (pin + 16)));
    assert_eq!(r.read(REG_ODR), 1 << pin);
}

#[test]
fn test_push_pull_disconnect() {
    let r = regs();
    let pin = 7usize;

    set_config(&r, pin, CFG_INPUT_FLOATING);
    r.gpio_set(pin, 1);
    assert_eq!(r.read(REG_IDR), 1 << pin);
    assert_eq!(r.disconnected_pins() & (1 << pin), 0);

    set_odr_bit(&r, pin, 0);
    set_config(&r, pin, CFG_OUTPUT_PP);
    assert_eq!(r.read(REG_IDR), 0);
    assert_ne!(r.disconnected_pins() & (1 << pin), 0);

    r.gpio_set(pin, 1);
    assert_eq!(r.read(REG_IDR), 0);
}

#[test]
fn test_analog_input_is_digitally_low() {
    let r = regs();
    let pin = 0;

    set_config(&r, pin, CFG_INPUT_ANALOG);
    r.gpio_set(pin, 1);

    assert_eq!(r.read(REG_IDR) & (1 << pin), 0);
}

#[test]
fn test_open_drain_released_samples_external_input() {
    let r = regs();
    let pin = 1;

    set_config(&r, pin, CFG_OUTPUT_OD);
    set_odr_bit(&r, pin, 1);

    r.gpio_set(pin, 1);
    assert_eq!(r.read(REG_IDR) & (1 << pin), 1 << pin);

    r.gpio_set(pin, 0);
    assert_eq!(r.read(REG_IDR) & (1 << pin), 0);
}

#[test]
fn test_af_output_does_not_follow_odr() {
    let r = regs();
    let pin = 2;

    set_config(&r, pin, CFG_AF_PP);
    set_odr_bit(&r, pin, 1);

    assert_eq!(r.read(REG_IDR) & (1 << pin), 0);
}

#[test]
fn test_reset_deasserts_previous_high_output() {
    let r = regs();
    let pin = 3;

    set_config(&r, pin, CFG_OUTPUT_PP);
    set_odr_bit(&r, pin, 1);
    assert_eq!(r.read(REG_IDR) & (1 << pin), 1 << pin);

    let update = r.reset();

    assert_ne!(update.mask & (1 << pin), 0);
    assert_eq!(update.levels & (1 << pin), 0);
}

#[test]
fn test_lckr_is_explicitly_unimplemented() {
    let r = regs();

    r.write(REG_LCKR, 0x0001_0001);

    assert_eq!(r.read(REG_LCKR), 0);
}
