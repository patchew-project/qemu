use std::ffi::CStr;

use bql::prelude::*;
use common::prelude::*;
use hwcore::prelude::*;
use qom::prelude::*;
use system::prelude::*;

pub const TYPE_CMSDK_APB_WATCHDOG: &CStr = c"cmsdk-apb-watchdog";

#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct CmsdkApbWatchdog {
    parent_obj: ParentField<SysBusDevice>,
    iomem: MemoryRegion,
    control: BqlRefCell<u32>,
    load:    BqlRefCell<u32>,
    value:   BqlRefCell<u32>,
    lock:    BqlRefCell<u32>,
}

impl CmsdkApbWatchdog {
    fn read(&self, offset: u64, _size: u32) -> u64 {
        match offset {
            0x0 => *self.load.borrow() as u64,
            0x4 => *self.value.borrow() as u64,
            0x8 => *self.control.borrow() as u64,
            0xC00 => *self.lock.borrow() as u64,
            _ => 0,
        }
    }

    fn write(&self, offset: u64, value: u64, _size: u32) {
        if *self.lock.borrow() == 1 && offset != 0xC00 {
            return;
        }

        match offset {
            0x0 => *self.load.borrow_mut() = value as u32,
            0x4 => {},
            0x8 => *self.control.borrow_mut() = value as u32,
            0xC00 => {
                if value == 0x1ACCE551 {
                    *self.lock.borrow_mut() = 0;
                } else {
                    *self.lock.borrow_mut() = 1;
                }
            },
            _ => {},
        }
    }

    fn reset(&self, _type: ResetType) {
        *self.control.borrow_mut() = 0;
        *self.load.borrow_mut() = 0;
        *self.value.borrow_mut() = 0;
        *self.lock.borrow_mut() = 0;
    }

    unsafe fn init(mut this: ParentInit<Self>) {
        static OPS: MemoryRegionOps<CmsdkApbWatchdog> =
            MemoryRegionOpsBuilder::<CmsdkApbWatchdog>::new()
                .read(&CmsdkApbWatchdog::read)
                .write(&CmsdkApbWatchdog::write)
                .little_endian()
                .impl_sizes(4, 4)
                .build();

        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &OPS,
            "cmsdk-apb-watchdog",
            0x1000,
        );

        uninit_field_mut!(*this, control).write(BqlRefCell::new(0));
        uninit_field_mut!(*this, load).write(BqlRefCell::new(0));
        uninit_field_mut!(*this, value).write(BqlRefCell::new(0));
        uninit_field_mut!(*this, lock).write(BqlRefCell::new(0));
    }

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
    }
}

qom_isa!(CmsdkApbWatchdog: SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for CmsdkApbWatchdog {
    type Class = <SysBusDevice as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = TYPE_CMSDK_APB_WATCHDOG;
}

impl ObjectImpl for CmsdkApbWatchdog {
    type ParentType = SysBusDevice;
    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl ResettablePhasesImpl for CmsdkApbWatchdog {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset);
}

impl DeviceImpl for CmsdkApbWatchdog {}
impl SysBusDeviceImpl for CmsdkApbWatchdog {}