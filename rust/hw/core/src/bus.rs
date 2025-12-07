// Copyright 2025 HUST OpenAtom Open Source Club.
// Author(s): Chen Miao <chenmiao@openatom.club>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ffi::CStr;

pub use bindings::BusClass;
use common::Opaque;
use qom::{qom_isa, IsA, Object, ObjectDeref, ObjectType};

use crate::{bindings, DeviceImpl};

#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct BusState(Opaque<bindings::BusState>);

unsafe impl Send for BusState {}
unsafe impl Sync for BusState {}

unsafe impl ObjectType for BusState {
    type Class = BusClass;
    const TYPE_NAME: &'static std::ffi::CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_BUS) };
}

qom_isa!(BusState: Object);

pub trait BusStateImpl: DeviceImpl + IsA<BusState> {}

impl BusClass {
    pub fn class_init<T: BusStateImpl>(self: &mut BusClass) {
        self.parent_class.class_init::<T>();
    }
}

pub trait BusMethods: ObjectDeref
where
    Self::Target: IsA<BusState>,
{
    // TODO: Since the bus does not currently provide services to other
    // components, we have not implemented any functions yet.
}

impl<R: ObjectDeref> BusMethods for R where R::Target: IsA<BusState> {}
