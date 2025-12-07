// Copyright 2025 HUST OpenAtom Open Source Club.
// Author(s): Chao Liu <chao.liu@openatom.club>
// Author(s): Chen Miao <chenmiao@openatom.club>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to access `i2c` functionality from Rust.

use std::{ffi::CStr, ptr::NonNull};

pub use crate::bindings::I2CSlaveClass;
use common::{self, Opaque};
use migration::impl_vmstate_c_struct;
use qom::{prelude::*, Owned};

use crate::{
    bindings,
    bus::{BusClass, BusState},
    qdev::{DeviceImpl, DeviceState},
};

/// A safe wrapper around [`bindings::I2CBus`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct I2CBus(Opaque<bindings::I2CBus>);

unsafe impl Send for I2CBus {}
unsafe impl Sync for I2CBus {}

unsafe impl ObjectType for I2CBus {
    type Class = BusClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_I2C_BUS) };
}

qom_isa!(I2CBus: BusState, Object);

/// Trait for methods of [`I2CBus`] and its subclasses.
pub trait I2CBusMethods: ObjectDeref
where
    Self::Target: IsA<I2CBus>,
{
    /// # Safety
    ///
    /// Initialize an I2C bus
    fn init_bus(&self, parent: &DeviceState, name: &str) -> Owned<I2CBus> {
        assert!(bql::is_locked());
        unsafe {
            let bus = bindings::i2c_init_bus(parent.as_mut_ptr(), name.as_ptr().cast());
            let bus: &I2CBus = I2CBus::from_raw(bus);
            Owned::from(bus)
        }
    }

    /// # Safety
    ///
    /// Start a transfer on an I2C bus
    fn start_transfer(&self, address: u8, is_recv: bool) -> i32 {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_start_transfer(self.upcast().as_mut_ptr(), address, is_recv) }
    }

    /// # Safety
    ///
    /// Start a receive transfer on an I2C bus
    fn start_recv(&self, address: u8) -> i32 {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_start_recv(self.upcast().as_mut_ptr(), address) }
    }

    /// # Safety
    ///
    /// Start a send transfer on an I2C bus
    fn start_send(&self, address: u8) -> i32 {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_start_send(self.upcast().as_mut_ptr(), address) }
    }

    /// # Safety
    ///
    /// End a transfer on an I2C bus
    fn end_transfer(&self) {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_end_transfer(self.upcast().as_mut_ptr()) }
    }

    /// # Safety
    ///
    /// Send NACK on an I2C bus
    fn nack(&self) {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_nack(self.upcast().as_mut_ptr()) }
    }

    /// # Safety
    ///
    /// Send ACK on an I2C bus
    fn ack(&self) {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_ack(self.upcast().as_mut_ptr()) }
    }

    /// # Safety
    ///
    /// Send data on an I2C bus
    fn send(&self, data: u8) -> i32 {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_send(self.upcast().as_mut_ptr(), data) }
    }

    /// # Safety
    ///
    /// Receive data from an I2C bus
    fn recv(&self) -> u8 {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_recv(self.upcast().as_mut_ptr()) }
    }

    /// # Safety
    ///
    /// Check if the I2C bus is busy.
    ///
    /// Returns `true` if the bus is busy, `false` otherwise.
    fn is_busy(&self) -> bool {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_bus_busy(self.upcast().as_mut_ptr()) != 0 }
    }

    /// # Safety
    ///
    /// Schedule pending master on an I2C bus
    fn schedule_pending_master(&self) {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_schedule_pending_master(self.upcast().as_mut_ptr()) }
    }

    /// Sets the I2C bus master.
    ///
    /// # Safety
    ///
    /// This function is unsafe because:
    /// - `bh` must be a valid pointer to a `QEMUBH`.
    /// - The caller must ensure that `self` is in a valid state.
    /// - The caller must guarantee no data races occur during execution.
    ///
    /// TODO ("`i2c_bus_master` missing until QEMUBH is wrapped")
    unsafe fn set_master(&self, bh: *mut bindings::QEMUBH) {
        assert!(bql::is_locked());
        unsafe { bindings::i2c_bus_master(self.upcast().as_mut_ptr(), bh) }
    }
}

impl<R: ObjectDeref> I2CBusMethods for R where R::Target: IsA<I2CBus> {}

/// A safe wrapper around [`bindings::I2CSlave`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct I2CSlave(Opaque<bindings::I2CSlave>);

unsafe impl Send for I2CSlave {}
unsafe impl Sync for I2CSlave {}

unsafe impl ObjectType for I2CSlave {
    type Class = I2CSlaveClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_I2C_SLAVE) };
}

qom_isa!(I2CSlave: DeviceState, Object);

impl_vmstate_c_struct!(I2CSlave, bindings::vmstate_i2c_slave);

#[derive(common::TryInto)]
#[repr(u64)]
#[allow(non_camel_case_types)]
pub enum I2CResult {
    ACK = 0,
    NACK = 1,
}

// TODO: add virtual methods
pub trait I2CSlaveImpl: DeviceImpl + IsA<I2CSlave> {
    /// Master to slave. Returns non-zero for a NAK, 0 for success.
    const SEND: Option<fn(&Self, data: u8) -> I2CResult> = None;

    /// Slave to master. This cannot fail, the device should always return something here.
    const RECV: Option<fn(&Self) -> u8> = None;

    /// Notify the slave of a bus state change. For start event,
    /// returns non-zero to NAK an operation. For other events the
    /// return code is not used and should be zero.
    const EVENT: Option<fn(&Self, event: I2CEvent) -> I2CEvent> = None;

    /// Check if this device matches the address provided. Returns bool of
    /// true if it matches (or broadcast), and updates the device list, false
    /// otherwise.
    ///
    /// If broadcast is true, match should add the device and return true.
    #[allow(clippy::type_complexity)]
    const MATCH_AND_ADD: Option<
        fn(&Self, address: u8, broadcast: bool, current_devs: *mut bindings::I2CNodeList) -> bool,
    > = None;
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_i2c_slave_send_fn<T: I2CSlaveImpl>(
    obj: *mut bindings::I2CSlave,
    data: u8,
) -> std::os::raw::c_int {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::SEND.unwrap()(unsafe { state.as_ref() }, data).into_bits() as std::os::raw::c_int
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_i2c_slave_recv_fn<T: I2CSlaveImpl>(obj: *mut bindings::I2CSlave) -> u8 {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::RECV.unwrap()(unsafe { state.as_ref() })
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_i2c_slave_event_fn<T: I2CSlaveImpl>(
    obj: *mut bindings::I2CSlave,
    event: bindings::i2c_event,
) -> std::os::raw::c_int {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::EVENT.unwrap()(unsafe { state.as_ref() }, I2CEvent::from_bits(event)).into_bits()
        as std::os::raw::c_int
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_i2c_slave_match_and_add_fn<T: I2CSlaveImpl>(
    obj: *mut bindings::I2CSlave,
    address: u8,
    broadcast: bool,
    current_devs: *mut bindings::I2CNodeList,
) -> bool {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::MATCH_AND_ADD.unwrap()(unsafe { state.as_ref() }, address, broadcast, current_devs)
}

impl I2CSlaveClass {
    /// Fill in the virtual methods of `I2CSlaveClass` based on the
    /// definitions in the `I2CSlaveImpl` trait.
    pub fn class_init<T: I2CSlaveImpl>(&mut self) {
        if <T as I2CSlaveImpl>::SEND.is_some() {
            self.send = Some(rust_i2c_slave_send_fn::<T>);
        }
        if <T as I2CSlaveImpl>::RECV.is_some() {
            self.recv = Some(rust_i2c_slave_recv_fn::<T>);
        }
        if <T as I2CSlaveImpl>::EVENT.is_some() {
            self.event = Some(rust_i2c_slave_event_fn::<T>);
        }
        if <T as I2CSlaveImpl>::MATCH_AND_ADD.is_some() {
            self.match_and_add = Some(rust_i2c_slave_match_and_add_fn::<T>);
        }
        self.parent_class.class_init::<T>();
    }
}

/// Trait for methods of [`I2CSlave`] and its subclasses.
pub trait I2CSlaveMethods: ObjectDeref
where
    Self::Target: IsA<I2CSlave>,
{
    /// Get the I2C bus address of a slave device
    fn get_address(&self) -> u8 {
        assert!(bql::is_locked());
        // SAFETY: the BQL ensures that no one else writes to the I2CSlave structure,
        // and the I2CSlave must be initialized to get an IsA<I2CSlave>.
        let slave = unsafe { *self.upcast().as_ptr() };
        slave.address
    }
}

impl<R: ObjectDeref> I2CSlaveMethods for R where R::Target: IsA<I2CSlave> {}

/// Enum representing I2C events
#[derive(common::TryInto)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum I2CEvent {
    START_RECV = bindings::I2C_START_RECV,
    START_SEND = bindings::I2C_START_SEND,
    START_SEND_ASYNC = bindings::I2C_START_SEND_ASYNC,
    FINISH = bindings::I2C_FINISH,
    NACK = bindings::I2C_NACK,
}
