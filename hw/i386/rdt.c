#include "qemu/osdep.h"
#include "hw/i386/rdt.h"
#include <stdint.h>
#include "hw/qdev-properties.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "target/i386/cpu.h"
#include "hw/isa/isa.h"

/* Max counts for allocation masks or CBMs. In other words, the size of respective MSRs*/
#define MAX_L3_MASK_COUNT      128
#define MAX_L2_MASK_COUNT      48
#define MAX_MBA_THRTL_COUNT    31

#define TYPE_RDT "rdt"
#define RDT_NUM_RMID_PROP "rmids"

OBJECT_DECLARE_TYPE(RDTState, RDTStateClass, RDT);

struct RDTMonitor {
    uint64_t count_local;
    uint64_t count_remote;
    uint64_t count_l3;
};

struct RDTAllocation {
    uint32_t active_cos;
};

struct RDTStateInstance {
    uint32_t active_rmid;
    GArray *monitors;

    RDTState *rdtstate;
};

struct RDTState {
    ISADevice parent;

    uint32_t rmids;

    GArray *rdtInstances;
    GArray *allocations;

    uint32_t msr_L3_ia32_mask_n[MAX_L3_MASK_COUNT];
    uint32_t msr_L2_ia32_mask_n[MAX_L2_MASK_COUNT];
    uint32_t ia32_L2_qos_ext_bw_thrtl_n[MAX_MBA_THRTL_COUNT];
};

struct RDTStateClass { };

OBJECT_DEFINE_TYPE(RDTState, rdt, RDT, ISA_DEVICE);

static Property rdt_properties[] = {
    DEFINE_PROP_UINT32(RDT_NUM_RMID_PROP, RDTState, rmids, 256),
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

