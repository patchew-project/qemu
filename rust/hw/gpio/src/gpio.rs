// STM32F1xx GPIO port device model (Rust)
// Copyright 2026, Jack Wang
// Author(s): Jack Wang <163wangjack@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reference: ST RM0041, section 7.

use std::ffi::CStr;

use bql::prelude::*;
use common::prelude::*;
use hwcore::prelude::*;
use migration::prelude::*;
use qom::prelude::*;
use system::prelude::*;
use util::prelude::*;

pub const GPIO_NUM_PINS: usize = 16;

pub const TYPE_STM32F1XX_GPIO_RUST: &CStr = c"stm32f1xx-gpio-rust";
qom_isa!(Stm32f1xxGpioState: SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for Stm32f1xxGpioState {
    type Class = <SysBusDevice as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = TYPE_STM32F1XX_GPIO_RUST;
}

::trace::include_trace!("hw_gpio");

pub const REG_CRL: u64 = 0x00;
pub const REG_CRH: u64 = 0x04;
pub const REG_IDR: u64 = 0x08;
pub const REG_ODR: u64 = 0x0C;
pub const REG_BSRR: u64 = 0x10;
pub const REG_BRR: u64 = 0x14;
pub const REG_LCKR: u64 = 0x18;

pub const RESET_CRL: u32 = 0x4444_4444;
pub const RESET_CRH: u32 = 0x4444_4444;

#[repr(C)]
#[derive(Debug)]
pub struct GpioRegisters {
    crl: BqlCell<u32>,
    crh: BqlCell<u32>,
    idr: BqlCell<u32>,
    odr: BqlCell<u32>,
    lckr: BqlCell<u32>,

    // Set bits have no external driver.
    disconnected_pins: BqlCell<u16>,
    pins_connected_high: BqlCell<u16>,
}

impl Default for GpioRegisters {
    fn default() -> Self {
        Self::new()
    }
}

/// Changed output pins and their new levels.
#[derive(Debug, Default, Clone, Copy)]
pub struct PinUpdate {
    pub mask: u32,
    pub levels: u32,
}

impl GpioRegisters {
    pub const fn new() -> Self {
        Self {
            crl: BqlCell::new(RESET_CRL),
            crh: BqlCell::new(RESET_CRH),
            idr: BqlCell::new(0),
            odr: BqlCell::new(0),
            lckr: BqlCell::new(0),
            disconnected_pins: BqlCell::new(0xFFFF),
            pins_connected_high: BqlCell::new(0),
        }
    }

    pub fn idr(&self) -> u32 {
        self.idr.get()
    }
    pub fn odr(&self) -> u32 {
        self.odr.get()
    }
    pub fn crl(&self) -> u32 {
        self.crl.get()
    }
    pub fn crh(&self) -> u32 {
        self.crh.get()
    }
    pub fn disconnected_pins(&self) -> u16 {
        self.disconnected_pins.get()
    }

    fn extract(value: u32, start: u32, length: u32) -> u32 {
        assert!(length > 0 && length <= 32 - start);
        (value >> start) & (!0u32 >> (32 - length))
    }

    fn get_modecnf(&self, pin: usize) -> u8 {
        let reg = if pin < 8 {
            self.crl.get()
        } else {
            self.crh.get()
        };
        let shift = ((pin % 8) * 4) as u32;
        Self::extract(reg, shift, 4) as u8
    }

    fn is_output(&self, pin: usize) -> bool {
        (self.get_modecnf(pin) & 0x03) != 0
    }

    fn is_alternate_open_drain(&self, pin: usize) -> bool {
        let modecnf = self.get_modecnf(pin);
        ((modecnf & 0x03) != 0) && ((modecnf >> 2) == 0x03)
    }

    fn is_alternate_push_pull(&self, pin: usize) -> bool {
        let modecnf = self.get_modecnf(pin);
        ((modecnf & 0x03) != 0) && ((modecnf >> 2) == 0x02)
    }

    fn is_general_push_pull(&self, pin: usize) -> bool {
        let modecnf = self.get_modecnf(pin);
        ((modecnf & 0x03) != 0) && ((modecnf >> 2) == 0x00)
    }

    fn is_general_open_drain(&self, pin: usize) -> bool {
        let modecnf = self.get_modecnf(pin);
        ((modecnf & 0x03) != 0) && ((modecnf >> 2) == 0x01)
    }

    fn is_analog_input(&self, pin: usize) -> bool {
        self.get_modecnf(pin) == 0b0000
    }

    fn is_odr_high(&self, pin: usize) -> bool {
        self.odr.get() & (1 << pin) != 0
    }

    fn is_input_pull_up_down_mode(&self, pin: usize) -> bool {
        self.get_modecnf(pin) == 0b1000
    }

    fn is_input_pullup(&self, pin: usize) -> bool {
        self.is_input_pull_up_down_mode(pin) && ((self.odr.get() & (1 << pin)) != 0)
    }

    fn is_input_pulldown(&self, pin: usize) -> bool {
        self.is_input_pull_up_down_mode(pin) && ((self.odr.get() & (1 << pin)) == 0)
    }

    fn check_and_warn_af(&self, start_pin: usize) {
        for i in 0..8 {
            let pin = start_pin + i;
            if self.is_alternate_push_pull(pin) || self.is_alternate_open_drain(pin) {
                log_mask_ln!(
                    Log::Unimp,
                    "stm32f1xx_gpio: alternate-function routing for pin {} is not implemented",
                    pin
                );
            }
        }
    }

    pub fn reset(&self) -> PinUpdate {
        self.crl.set(RESET_CRL);
        self.crh.set(RESET_CRH);
        self.idr.set(0);
        self.odr.set(0);
        self.lckr.set(0);
        self.disconnected_pins.set(0xFFFF);
        self.pins_connected_high.set(0);

        // RM0041 leaves IDR undefined; use low and deassert every output.
        PinUpdate {
            mask: 0xFFFF,
            levels: 0,
        }
    }

    pub fn gpio_set(&self, line: usize, level: u32) -> PinUpdate {
        let push_pull = self.is_general_push_pull(line) || self.is_alternate_push_pull(line);
        let open_drain_drives_low = self.is_general_open_drain(line) && !self.is_odr_high(line);

        if push_pull || (open_drain_drives_low && level != 0) {
            log_mask_ln!(
                Log::GuestError,
                "stm32f1xx_gpio: line {} cannot be driven to {} externally",
                line,
                level
            );
            return PinUpdate::default();
        }

        self.disconnected_pins
            .set(self.disconnected_pins.get() & !(1 << line));

        if level != 0 {
            self.pins_connected_high
                .set(self.pins_connected_high.get() | (1 << line));
        } else {
            self.pins_connected_high
                .set(self.pins_connected_high.get() & !(1 << line));
        }

        self.update_gpio_idr()
    }

    // Retain an open-drain source so it can be sampled after release.
    fn get_gpio_pinmask_to_disconnect(&self) -> u16 {
        let mut pins_to_disconnect: u16 = 0;
        for i in 0..GPIO_NUM_PINS {
            if (self.disconnected_pins.get() & (1 << i) == 0) && self.is_output(i) {
                if self.is_general_push_pull(i)
                    || self.is_alternate_push_pull(i)
                    || self.is_alternate_open_drain(i)
                {
                    pins_to_disconnect |= 1 << i;
                    log_mask_ln!(
                        Log::GuestError,
                        "stm32f1xx_gpio: Line {} can't be driven externally",
                        i
                    );
                }
            }
        }
        pins_to_disconnect
    }

    fn disconnect_gpio_pins(&self, lines: u16) -> PinUpdate {
        self.disconnected_pins
            .set(self.disconnected_pins.get() | lines);
        self.update_gpio_idr()
    }

    pub fn write(&self, offset: u64, data: u32) -> PinUpdate {
        match offset {
            REG_CRL => {
                self.crl.set(data);
                self.check_and_warn_af(0);
                self.disconnect_gpio_pins(self.get_gpio_pinmask_to_disconnect())
            }
            REG_CRH => {
                self.crh.set(data);
                self.check_and_warn_af(8);
                self.disconnect_gpio_pins(self.get_gpio_pinmask_to_disconnect())
            }
            REG_ODR => {
                self.odr.set(data & 0xFFFF);
                self.update_gpio_idr()
            }
            REG_BRR => {
                let bits_to_reset = data & 0xFFFF;
                self.odr.set(self.odr.get() & !bits_to_reset);
                self.update_gpio_idr()
            }
            REG_BSRR => {
                let bits_to_set = data & 0xFFFF;
                let bits_to_reset = (data >> 16) & 0xFFFF;

                // If both BSx and BRx are set, BSx has priority.
                let mut current_odr = self.odr.get();
                current_odr &= !bits_to_reset;
                current_odr |= bits_to_set;
                self.odr.set(current_odr);

                self.update_gpio_idr()
            }
            REG_LCKR => {
                log_mask_ln!(
                    Log::Unimp,
                    "stm32f1xx_gpio: LCKR configuration locking is not implemented"
                );
                PinUpdate::default()
            }
            _ => {
                log_mask_ln!(
                    Log::GuestError,
                    "stm32f1xx_gpio_write: Bad offset 0x{:x}",
                    offset
                );
                PinUpdate::default()
            }
        }
    }

    pub fn read(&self, offset: u64) -> u32 {
        match offset {
            REG_CRL => self.crl.get(),
            REG_CRH => self.crh.get(),
            REG_IDR => self.idr.get(),
            REG_ODR => self.odr.get(),
            REG_BSRR => 0,
            REG_BRR => 0,
            REG_LCKR => 0,
            _ => {
                log_mask_ln!(
                    Log::GuestError,
                    "stm32f1xx_gpio_read: Bad offset 0x{:x}",
                    offset
                );
                0
            }
        }
    }

    fn update_gpio_idr(&self) -> PinUpdate {
        let old_idr = self.idr.get();
        let mut new_idr = old_idr;
        let mut new_idr_mask = 0u32;

        for i in 0..GPIO_NUM_PINS {
            let bit = 1u32 << i;
            let pin_bit = 1u16 << i;
            let external_connected = self.disconnected_pins.get() & pin_bit == 0;
            let external_high = self.pins_connected_high.get() & pin_bit != 0;

            if self.is_analog_input(i) {
                new_idr &= !bit;
                new_idr_mask |= bit;
            } else if self.is_alternate_push_pull(i) || self.is_alternate_open_drain(i) {
                // AF routing is not modelled.
                new_idr &= !bit;
                new_idr_mask |= bit;
            } else if self.is_general_push_pull(i) {
                if self.is_odr_high(i) {
                    new_idr |= bit;
                } else {
                    new_idr &= !bit;
                }
                new_idr_mask |= bit;
            } else if self.is_general_open_drain(i) {
                if !self.is_odr_high(i) {
                    new_idr &= !bit;
                } else if external_connected && external_high {
                    new_idr |= bit;
                } else {
                    new_idr &= !bit;
                }
                new_idr_mask |= bit;
            } else if external_connected {
                if external_high {
                    new_idr |= bit;
                } else {
                    new_idr &= !bit;
                }
                new_idr_mask |= bit;
            } else if self.is_input_pullup(i) {
                new_idr |= bit;
                new_idr_mask |= bit;
            } else if self.is_input_pulldown(i) {
                new_idr &= !bit;
                new_idr_mask |= bit;
            }
        }

        let resolved_idr = (old_idr & !new_idr_mask) | (new_idr & new_idr_mask);
        self.idr.set(resolved_idr);

        PinUpdate {
            mask: (old_idr ^ resolved_idr) & new_idr_mask,
            levels: resolved_idr,
        }
    }
}

impl_vmstate_struct!(
    GpioRegisters,
    VMStateDescriptionBuilder::<GpioRegisters>::new()
        .name(c"stm32f1xx-gpio/regs")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(GpioRegisters, crl),
            vmstate_of!(GpioRegisters, crh),
            vmstate_of!(GpioRegisters, idr),
            vmstate_of!(GpioRegisters, odr),
            vmstate_of!(GpioRegisters, lckr),
            vmstate_of!(GpioRegisters, disconnected_pins),
            vmstate_of!(GpioRegisters, pins_connected_high),
        })
        .build()
);

#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct Stm32f1xxGpioState {
    parent_obj: ParentField<SysBusDevice>,
    mmio: MemoryRegion,

    regs: GpioRegisters,

    outlines: [InterruptSource; GPIO_NUM_PINS],
}

impl Stm32f1xxGpioState {
    unsafe fn init(mut this: ParentInit<Self>) {
        static STM32F1XX_GPIO_OPS: MemoryRegionOps<Stm32f1xxGpioState> =
            MemoryRegionOpsBuilder::<Stm32f1xxGpioState>::new()
                .read(&Stm32f1xxGpioState::read)
                .write(&Stm32f1xxGpioState::write)
                .little_endian()
                .valid_sizes(4, 4)
                .impl_sizes(4, 4)
                .build();

        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, mmio),
            &STM32F1XX_GPIO_OPS,
            "stm32f1xx-gpio-rust",
            0x400,
        );

        uninit_field_mut!(*this, regs).write(GpioRegisters::new());
        uninit_field_mut!(*this, outlines).write(Default::default());

        trace::trace_stm32f1xx_gpio_instance_init();
    }

    fn post_init(&self) {
        self.init_mmio(&self.mmio);
        // Pin interrupts are routed through EXTI, not a sysbus IRQ.
        self.init_gpio_in(GPIO_NUM_PINS as u32, Self::gpio_set);
        self.init_gpio_out(&self.outlines);
    }

    fn apply_pin_update(&self, upd: PinUpdate) {
        for i in 0..GPIO_NUM_PINS {
            if (upd.mask & (1 << i)) != 0 {
                let level = (upd.levels & (1 << i)) != 0;
                self.outlines[i].set(level);
                trace::trace_stm32f1xx_gpio_irq(i as u32, i32::from(level));
            }
        }
    }

    fn reset_hold(&self, _type: ResetType) {
        trace::trace_stm32f1xx_gpio_reset();
        let upd = self.regs.reset();
        self.apply_pin_update(upd);
    }

    fn gpio_set(&self, line: u32, level: u32) {
        let upd = self.regs.gpio_set(line as usize, level);
        self.apply_pin_update(upd);
    }

    fn write(&self, offset: hwaddr, data: u64, size: u32) {
        trace::trace_stm32f1xx_gpio_write(offset, data, size);
        let upd = self.regs.write(offset, data as u32);
        self.apply_pin_update(upd);
    }

    fn read(&self, offset: hwaddr, size: u32) -> u64 {
        let value = self.regs.read(offset);
        trace::trace_stm32f1xx_gpio_read(offset, u64::from(value), size);
        u64::from(value)
    }
}

impl ObjectImpl for Stm32f1xxGpioState {
    type ParentType = SysBusDevice;

    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
}

impl DeviceImpl for Stm32f1xxGpioState {
    const VMSTATE: Option<VMStateDescription<Self>> = Some(VMSTATE_STM32F1XX_GPIO);
}

impl SysBusDeviceImpl for Stm32f1xxGpioState {}

impl ResettablePhasesImpl for Stm32f1xxGpioState {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset_hold);
}

const VMSTATE_STM32F1XX_GPIO: VMStateDescription<Stm32f1xxGpioState> =
    VMStateDescriptionBuilder::<Stm32f1xxGpioState>::new()
        .name(c"stm32f1xx-gpio")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(Stm32f1xxGpioState, regs),
        })
        .build();
