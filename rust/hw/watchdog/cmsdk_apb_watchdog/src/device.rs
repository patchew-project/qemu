use std::ffi::CStr;

use bql::prelude::*;
use common::prelude::*; 
use hwcore::prelude::*;
use qom::prelude::*;
use system::prelude::*;

// 1. Type Name
pub const TYPE_CMSDK_APB_WATCHDOG: &CStr = c"cmsdk-apb-watchdog";

// 2. The Struct
#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct CmsdkApbWatchdog {
    parent_obj: ParentField<SysBusDevice>,
    iomem: MemoryRegion,
    control: BqlCell<u32>,
    load:    BqlCell<u32>,
    value:   BqlCell<u32>,
    lock:    BqlCell<u32>,
}

// 3. Logic Implementation
impl CmsdkApbWatchdog {
    fn read(&self, offset: u64, _size: u32) -> u64 {
        match offset {
            0x0 => self.load.get() as u64,
            0x4 => self.value.get() as u64,
            0x8 => self.control.get() as u64,
            0xC00 => self.lock.get() as u64,
            _ => 0,
        }
    }

    fn write(&self, offset: u64, value: u64, _size: u32) {
        if self.lock.get() == 1 && offset != 0xC00 {
            return;
        }

        match offset {
            0x0 => self.load.set(value as u32),
            0x4 => {}, 
            0x8 => self.control.set(value as u32),
            0xC00 => {
                if value == 0x1ACCE551 {
                    self.lock.set(0);
                } else {
                    self.lock.set(1);
                }
            },
            _ => {},
        }
    }

    fn reset(&self, _type: ResetType) {
        self.control.set(0);
        self.load.set(0);
        self.value.set(0);
        self.lock.set(0);
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
        
        uninit_field_mut!(*this, control).write(BqlCell::new(0));
        uninit_field_mut!(*this, load).write(BqlCell::new(0));
        uninit_field_mut!(*this, value).write(BqlCell::new(0));
        uninit_field_mut!(*this, lock).write(BqlCell::new(0));
    }
    
    fn post_init(&self) {
        self.init_mmio(&self.iomem);
    }
}

// 4. Registration Glue
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