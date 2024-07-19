#include "qemu/osdep.h"
#include "hw/i386/rdt.h"
#include <stdint.h>
#include "hw/qdev-properties.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "target/i386/cpu.h"
#include "hw/isa/isa.h"

/* RDT Monitoring Event Codes */
#define RDT_EVENT_L3_OCCUPANCY 1
#define RDT_EVENT_L3_REMOTE_BW 2
#define RDT_EVENT_L3_LOCAL_BW 3

/* Max counts for allocation masks or CBMs. In other words, the size of respective MSRs*/
#define MAX_L3_MASK_COUNT      128
#define MAX_L2_MASK_COUNT      48
#define MAX_MBA_THRTL_COUNT    31

/* RDT L3 Allocation features */
#define CPUID_10_1_EAX_CBM_LENGTH       0xf
#define CPUID_10_1_EBX_CBM              0x0
#define CPUID_10_1_ECX_CDP              0x0 // to enable, it would be (1U << 2)
#define CPUID_10_1_EDX_COS_MAX          MAX_L3_MASK_COUNT
/* RDT L2 Allocation features*/
#define CPUID_10_2_EAX_CBM_LENGTH       0xf
#define CPUID_10_2_EBX_CBM              0x0
#define CPUID_10_2_EDX_COS_MAX          MAX_L2_MASK_COUNT
/* RDT MBA features */
#define CPUID_10_3_EAX_THRTL_MAX        89
#define CPUID_10_3_ECX_LINEAR_RESPONSE (1U << 2)
#define CPUID_10_3_EDX_COS_MAX          MAX_MBA_THRTL_COUNT

#define TYPE_RDT "rdt"
#define RDT_NUM_RMID_PROP "rmids"

#define QM_CTR_Error        (1ULL << 63)
#define QM_CTR_Unavailable  (1ULL << 62)

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

uint32_t rdt_get_cpuid_15_0_edx_l3(void) { return CPUID_15_1_EDX_L3_OCCUPANCY | CPUID_15_1_EDX_L3_TOTAL_BW | CPUID_15_1_EDX_L3_LOCAL_BW; }

uint32_t rdt_cpuid_15_1_edx_l3_total_bw_enabled(void) { return CPUID_15_1_EDX_L3_TOTAL_BW; }
uint32_t rdt_cpuid_15_1_edx_l3_local_bw_enabled(void) { return CPUID_15_1_EDX_L3_LOCAL_BW; }
uint32_t rdt_cpuid_15_1_edx_l3_occupancy_enabled(void) { return CPUID_15_1_EDX_L3_OCCUPANCY; }

uint32_t rdt_cpuid_10_0_ebx_l3_cat_enabled(void) { return CPUID_10_0_EBX_L3_CAT; }
uint32_t rdt_cpuid_10_0_ebx_l2_cat_enabled(void) { return CPUID_10_0_EBX_L2_CAT; }
uint32_t rdt_cpuid_10_0_ebx_l2_mba_enabled(void) { return CPUID_10_0_EBX_MBA; }

uint32_t rdt_get_cpuid_10_1_eax_cbm_length(void) { return CPUID_10_1_EAX_CBM_LENGTH; }
uint32_t rdt_cpuid_10_1_ebx_cbm_enabled(void) { return CPUID_10_1_EBX_CBM; }
uint32_t rdt_cpuid_10_1_ecx_cdp_enabled(void) { return CPUID_10_1_ECX_CDP; }
uint32_t rdt_get_cpuid_10_1_edx_cos_max(void) { return CPUID_10_1_EDX_COS_MAX; }

uint32_t rdt_get_cpuid_10_2_eax_cbm_length(void) { return CPUID_10_2_EAX_CBM_LENGTH; }
uint32_t rdt_cpuid_10_2_ebx_cbm_enabled(void) { return CPUID_10_2_EBX_CBM; }
uint32_t rdt_get_cpuid_10_2_edx_cos_max(void) { return CPUID_10_2_EDX_COS_MAX; }

uint32_t rdt_get_cpuid_10_3_eax_thrtl_max(void) { return CPUID_10_3_EAX_THRTL_MAX; }
uint32_t rdt_cpuid_10_3_eax_linear_response_enabled(void) { return CPUID_10_3_ECX_LINEAR_RESPONSE; }
uint32_t rdt_get_cpuid_10_3_edx_cos_max(void) { return CPUID_10_3_EDX_COS_MAX; }

bool rdt_associate_rmid_cos(uint64_t msr_ia32_pqr_assoc) {
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;
    RDTAllocation *alloc;

    uint32_t cos_id = (msr_ia32_pqr_assoc & 0xffff0000) >> 16;
    uint32_t rmid = msr_ia32_pqr_assoc & 0xffff;

    if (cos_id > MAX_L3_MASK_COUNT || cos_id > MAX_L2_MASK_COUNT ||
    cos_id > MAX_MBA_THRTL_COUNT || rmid > rdt_max_rmid(rdt)) {
        return false;
    }

    rdt->active_rmid = rmid;

    alloc = &g_array_index(rdt->rdtstate->allocations, RDTAllocation, rmid);

    alloc->active_cos = cos_id;

    return true;
}

uint32_t rdt_read_l3_mask(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    uint32_t val = rdt->rdtstate->msr_L3_ia32_mask_n[pos];
    return val;
}

uint32_t rdt_read_l2_mask(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    uint32_t val = rdt->rdtstate->msr_L2_ia32_mask_n[pos];
    return val;
}

uint32_t rdt_read_mba_thrtl(uint32_t pos)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    uint32_t val = rdt->rdtstate->ia32_L2_qos_ext_bw_thrtl_n[pos];
    return val;
}

void rdt_write_msr_l3_mask(uint32_t pos, uint32_t val) {
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    rdt->rdtstate->msr_L3_ia32_mask_n[pos] = val;
}

void rdt_write_msr_l2_mask(uint32_t pos, uint32_t val) {
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    rdt->rdtstate->msr_L2_ia32_mask_n[pos] = val;
}

void rdt_write_mba_thrtl(uint32_t pos, uint32_t val) {
    X86CPU *cpu = X86_CPU(current_cpu);
    RDTStateInstance *rdt = cpu->rdt;

    rdt->rdtstate->ia32_L2_qos_ext_bw_thrtl_n[pos] = val;
}

uint32_t rdt_max_rmid(RDTStateInstance *rdt)
{
    RDTState *rdtdev = rdt->rdtstate;
    return rdtdev->rmids - 1;
}

uint64_t rdt_read_event_count(RDTStateInstance *rdtInstance, uint32_t rmid, uint32_t event_id)
{
    CPUState *cs;
    RDTMonitor *mon;
    RDTState *rdt = rdtInstance->rdtstate;

    uint32_t count_l3 = 0;
    uint32_t count_local= 0;
    uint32_t count_remote = 0;

    if (!rdt) {
        return 0;
    }

    CPU_FOREACH(cs) {
        rdtInstance = &g_array_index(rdt->rdtInstances, RDTStateInstance, cs->cpu_index);
        if (rmid >= rdtInstance->monitors->len) {
            return QM_CTR_Error;
        }
        mon = &g_array_index(rdtInstance->monitors, RDTMonitor, rmid);
        count_l3 += mon->count_l3;
        count_local += mon->count_local;
        count_remote += mon->count_remote;
    }

    switch (event_id) {
        case RDT_EVENT_L3_OCCUPANCY:
            return count_l3 == 0 ? QM_CTR_Unavailable : count_l3;
            break;
        case RDT_EVENT_L3_REMOTE_BW:
            return count_remote == 0 ? QM_CTR_Unavailable : count_remote;
            break;
        case RDT_EVENT_L3_LOCAL_BW:
            return count_local == 0 ? QM_CTR_Unavailable : count_local;
            break;
        default:
            return QM_CTR_Error;
    }
}

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
    CPUState *cs = first_cpu;

    RDTState *rdtDev = RDT(dev);
    rdtDev->rdtInstances = g_array_new(false, true, sizeof(RDTStateInstance));
    g_array_set_size(rdtDev->rdtInstances, cs->nr_cores);
    CPU_FOREACH(cs) {
        RDTStateInstance *rdt = &g_array_index(rdtDev->rdtInstances, RDTStateInstance, cs->cpu_index);

        X86CPU *cpu = X86_CPU(cs);
        rdt->rdtstate = rdtDev;
        cpu->rdt = rdt;

        rdt->monitors = g_array_new(false, true, sizeof(RDTMonitor));
        rdt->rdtstate->allocations = g_array_new(false, true, sizeof(RDTAllocation));

        g_array_set_size(rdt->monitors, rdtDev->rmids);
        g_array_set_size(rdt->rdtstate->allocations, rdtDev->rmids);
    }
}

static void rdt_finalize(Object *obj)
{
    CPUState *cs;
    RDTState *rdt = RDT(obj);

    CPU_FOREACH(cs) {
        RDTStateInstance *rdtInstance = &g_array_index(rdt->rdtInstances, RDTStateInstance, cs->cpu_index);
        g_array_free(rdtInstance->monitors, true);
        g_array_free(rdtInstance->rdtstate->allocations, true);
    }

    g_array_free(rdt->rdtInstances, true);
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

