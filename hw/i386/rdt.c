#include "qemu/osdep.h"
#include "hw/i386/rdt.h"
#include <stdint.h>
#include "hw/qdev-properties.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "target/i386/cpu.h"
#include "hw/isa/isa.h"

#define TYPE_RDT "rdt"

OBJECT_DECLARE_TYPE(RDTState, RDTStateClass, RDT);

struct RDTState {
    ISADevice parent;
};

struct RDTStateClass { };

OBJECT_DEFINE_TYPE(RDTState, rdt, RDT, ISA_DEVICE);

static Property rdt_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void rdt_init(Object *obj)
{
}

static void rdt_realize(DeviceState *dev, Error **errp)
{
}

static void rdt_finalize(Object *obj)
{
}

static void rdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->hotpluggable = false;
    dc->desc = "RDT";
    dc->user_creatable = true;
    dc->realize = rdt_realize;

    device_class_set_props(dc, rdt_properties);
}

