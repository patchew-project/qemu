use std::{ffi::CStr, sync::LazyLock};

use qom::{qom_isa, Object, ObjectClassMethods, ObjectImpl, ObjectType, ParentField};
use util::bindings::{module_call_init, module_init_type};

#[repr(C)]
#[derive(qemu_macros::Object)]
pub struct DummyObject {
    parent: ParentField<Object>,
}

qom_isa!(DummyObject: Object);

pub struct DummyClass {}

impl DummyClass {
    pub fn class_init(self: &mut DummyClass) {
        //
    }
}

unsafe impl ObjectType for DummyObject {
    type Class = DummyClass;
    const TYPE_NAME: &'static CStr = c"dummy";
}

impl ObjectImpl for DummyObject {
    type ParentType = Object;
    const ABSTRACT: bool = false;
    const CLASS_INIT: fn(&mut DummyClass) = DummyClass::class_init;
}

fn init_qom() {
    static ONCE: LazyLock<()> = LazyLock::new(|| unsafe {
        module_call_init(module_init_type::MODULE_INIT_QOM);
    });

    bql::start_test();
    LazyLock::force(&ONCE);
}

#[test]
/// Create and immediately drop an instance.
fn test_object_new() {
    init_qom();
    drop(DummyObject::new());
}
