// Copyright 2025 HUST OpenAtom Open Source Club.
// Author(s): Chen Miao <chenmiao@openatom.club>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::slice::from_ref;

use bql::BqlRefCell;
use common::bitops::IntegerExt;
use hwcore::{
    DeviceClass, DeviceImpl, DeviceMethods, DeviceState, I2CResult, I2CSlave, I2CSlaveImpl,
    InterruptSource, ResetType, ResettablePhasesImpl,
};
use migration::{
    self, impl_vmstate_struct, vmstate_fields, vmstate_of, VMStateDescription,
    VMStateDescriptionBuilder,
};
use qom::{qom_isa, IsA, Object, ObjectImpl, ObjectType, ParentField};

pub const TYPE_PCF8574: &::std::ffi::CStr = c"pcf8574";
const PORTS_COUNT: usize = 8;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PCF8574Inner {
    pub lastrq: u8,
    pub input: u8,
    pub output: u8,
}

impl PCF8574Inner {
    pub fn line_state(&self) -> u8 {
        self.input & self.output
    }

    pub fn set_output(&mut self, data: u8) -> (u8, u8) {
        let prev = self.line_state();
        self.output = data;
        let actual = self.line_state();
        (prev, actual)
    }

    pub fn set_input(&mut self, start: u32, value: u8) -> bool {
        self.input = self.input.deposit(start, 1, value);
        self.has_state_changed()
    }

    pub fn receive(&mut self) -> (bool, u8) {
        let state_changed = self.has_state_changed();
        if state_changed {
            self.lastrq = self.line_state();
        }
        (state_changed, self.lastrq)
    }

    pub fn has_state_changed(&self) -> bool {
        self.line_state() != self.lastrq
    }
}

#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct PCF8574State {
    pub parent_obj: ParentField<I2CSlave>,
    pub inner: BqlRefCell<PCF8574Inner>,
    pub handler: [InterruptSource; PORTS_COUNT],
    pub intrq: InterruptSource,
}

// static_assert!(size_of::<PCF8574State>() <= size_of::<crate::bindings::PCF8574State>());

qom_isa!(PCF8574State: I2CSlave, DeviceState, Object);

#[allow(dead_code)]
trait PCF8574Impl: I2CSlaveImpl + IsA<PCF8574State> {}

unsafe impl ObjectType for PCF8574State {
    type Class = DeviceClass;
    const TYPE_NAME: &'static std::ffi::CStr = crate::TYPE_PCF8574;
}

impl PCF8574Impl for PCF8574State {}

impl ObjectImpl for PCF8574State {
    type ParentType = I2CSlave;
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl DeviceImpl for PCF8574State {
    const VMSTATE: Option<migration::VMStateDescription<Self>> = Some(VMSTATE_PCF8574);
    const REALIZE: Option<fn(&Self) -> util::Result<()>> = Some(Self::realize);
}

impl ResettablePhasesImpl for PCF8574State {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset_hold);
}

impl I2CSlaveImpl for PCF8574State {
    const SEND: Option<fn(&Self, data: u8) -> I2CResult> = Some(Self::send);
    const RECV: Option<fn(&Self) -> u8> = Some(Self::recv);
}

impl PCF8574State {
    fn send(&self, data: u8) -> I2CResult {
        let (prev, actual) = self.inner.borrow_mut().set_output(data);

        let mut diff = actual ^ prev;
        while diff != 0 {
            let line = diff.trailing_zeros() as u8;
            if let Some(handler) = self.handler.get(line as usize) {
                handler.set((actual >> line) & 1 == 1);
            }
            diff &= !(1 << line);
        }

        self.intrq.set(actual == self.inner.borrow().lastrq);

        I2CResult::ACK
    }

    fn recv(&self) -> u8 {
        let (has_changed, actual) = self.inner.borrow_mut().receive();
        if has_changed {
            self.intrq.raise();
        }

        actual
    }

    fn realize(&self) -> util::Result<()> {
        self.init_gpio_in(self.handler_size(), PCF8574State::gpio_set);
        self.init_gpio_out(from_ref(&self.handler[0]));
        self.init_gpio_out_named(from_ref(&self.intrq), "nINT", 1);
        Ok(())
    }

    fn gpio_set(&self, line: u32, level: u32) {
        assert!(line < self.handler_size());

        if self
            .inner
            .borrow_mut()
            .set_input(line, u8::from(level != 0))
        {
            self.intrq.raise();
        }
    }

    fn handler_size(&self) -> u32 {
        self.handler.len() as u32
    }

    fn reset_hold(&self, _type: ResetType) {}
}

impl_vmstate_struct!(
    PCF8574Inner,
    VMStateDescriptionBuilder::<PCF8574Inner>::new()
        .name(c"pcf8574/inner")
        .version_id(0)
        .minimum_version_id(0)
        .fields(vmstate_fields! {
            vmstate_of!(PCF8574Inner, lastrq),
            vmstate_of!(PCF8574Inner, input),
            vmstate_of!(PCF8574Inner, output),
        })
        .build()
);

pub const VMSTATE_PCF8574: VMStateDescription<PCF8574State> =
    VMStateDescriptionBuilder::<PCF8574State>::new()
        .name(c"pcf8574")
        .version_id(0)
        .minimum_version_id(0)
        .fields(vmstate_fields! {
            vmstate_of!(PCF8574State, parent_obj),
            vmstate_of!(PCF8574State, inner),
        })
        .build();
