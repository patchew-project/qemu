/*
 * QEMU monitor.c for ARM.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "kvm_arm.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "qom/qom-qobject.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "internals.h"

static GICCapability *gic_cap_new(int version)
{
    GICCapability *cap = g_new0(GICCapability, 1);
    cap->version = version;
    /* by default, support none */
    cap->emulated = false;
    cap->kernel = false;
    return cap;
}

static GICCapabilityList *gic_cap_list_add(GICCapabilityList *head,
                                           GICCapability *cap)
{
    GICCapabilityList *item = g_new0(GICCapabilityList, 1);
    item->value = cap;
    item->next = head;
    return item;
}

static inline void gic_cap_kvm_probe(GICCapability *v2, GICCapability *v3)
{
#ifdef CONFIG_KVM
    int fdarray[3];

    if (!kvm_arm_create_scratch_host_vcpu(NULL, fdarray, NULL)) {
        return;
    }

    /* Test KVM GICv2 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V2)) {
        v2->kernel = true;
    }

    /* Test KVM GICv3 */
    if (kvm_device_supported(fdarray[1], KVM_DEV_TYPE_ARM_VGIC_V3)) {
        v3->kernel = true;
    }

    kvm_arm_destroy_scratch_host_vcpu(fdarray);
#endif
}

GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    GICCapabilityList *head = NULL;
    GICCapability *v2 = gic_cap_new(2), *v3 = gic_cap_new(3);

    v2->emulated = true;
    v3->emulated = true;

    gic_cap_kvm_probe(v2, v3);

    head = gic_cap_list_add(head, v2);
    head = gic_cap_list_add(head, v3);

    return head;
}

QEMU_BUILD_BUG_ON(ARM_MAX_VQ > 16);

/*
 * These are cpu model features we want to advertise. The order here
 * matters as this is the order in which qmp_query_cpu_model_expansion
 * will attempt to set them. If there are dependencies between features,
 * then the order that considers those dependencies must be used.
 */
static const char *cpu_model_advertised_features[] = {
    "aarch64", "pmu", "sve",
    "sve128", "sve256", "sve384", "sve512",
    "sve640", "sve768", "sve896", "sve1024", "sve1152", "sve1280",
    "sve1408", "sve1536", "sve1664", "sve1792", "sve1920", "sve2048",
    "kvm-no-adjvtime", "kvm-steal-time",
    NULL
};

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    CpuModelExpansionInfo *expansion_info;
    const QDict *qdict_in = NULL;
    QDict *qdict_out;
    ObjectClass *oc;
    Object *obj;
    const char *name;
    int i;

    if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }

    if (!kvm_enabled() && !strcmp(model->name, "host")) {
        error_setg(errp, "The CPU type '%s' requires KVM", model->name);
        return NULL;
    }

    oc = cpu_class_by_name(TYPE_ARM_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a recognized ARM CPU type",
                   model->name);
        return NULL;
    }

    if (kvm_enabled()) {
        bool supported = false;

        if (!strcmp(model->name, "host") || !strcmp(model->name, "max")) {
            /* These are kvmarm's recommended cpu types */
            supported = true;
        } else if (current_machine->cpu_type) {
            const char *cpu_type = current_machine->cpu_type;
            int len = strlen(cpu_type) - strlen(ARM_CPU_TYPE_SUFFIX);

            if (strlen(model->name) == len &&
                !strncmp(model->name, cpu_type, len)) {
                /* KVM is enabled and we're using this type, so it works. */
                supported = true;
            }
        }
        if (!supported) {
            error_setg(errp, "We cannot guarantee the CPU type '%s' works "
                             "with KVM on this host", model->name);
            return NULL;
        }
    }

    if (model->props) {
        qdict_in = qobject_to(QDict, model->props);
        if (!qdict_in) {
            error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            return NULL;
        }
    }

    obj = object_new(object_class_get_name(oc));

    if (qdict_in) {
        Visitor *visitor;
        Error *err = NULL;

        visitor = qobject_input_visitor_new(model->props);
        if (!visit_start_struct(visitor, NULL, NULL, 0, errp)) {
            visit_free(visitor);
            object_unref(obj);
            return NULL;
        }

        i = 0;
        while ((name = cpu_model_advertised_features[i++]) != NULL) {
            if (qdict_get(qdict_in, name)) {
                if (!object_property_set(obj, name, visitor, &err)) {
                    break;
                }
            }
        }

        if (!err) {
            visit_check_struct(visitor, &err);
        }
        if (!err) {
            arm_cpu_finalize_features(ARM_CPU(obj), &err);
        }
        visit_end_struct(visitor, NULL);
        visit_free(visitor);
        if (err) {
            object_unref(obj);
            error_propagate(errp, err);
            return NULL;
        }
    } else {
        arm_cpu_finalize_features(ARM_CPU(obj), &error_abort);
    }

    expansion_info = g_new0(CpuModelExpansionInfo, 1);
    expansion_info->model = g_malloc0(sizeof(*expansion_info->model));
    expansion_info->model->name = g_strdup(model->name);

    qdict_out = qdict_new();

    i = 0;
    while ((name = cpu_model_advertised_features[i++]) != NULL) {
        ObjectProperty *prop = object_property_find(obj, name);
        if (prop) {
            QObject *value;

            assert(prop->get);
            value = object_property_get_qobject(obj, name, &error_abort);

            qdict_put_obj(qdict_out, name, value);
        }
    }

    if (!qdict_size(qdict_out)) {
        qobject_unref(qdict_out);
    } else {
        expansion_info->model->props = QOBJECT(qdict_out);
        expansion_info->model->has_props = true;
    }

    object_unref(obj);

    return expansion_info;
}

/* Perform linear address sign extension */
static target_ulong addr_canonical(int va_bits, target_ulong addr)
{
#ifdef TARGET_AARCH64
    if (addr & (1UL << (va_bits - 1))) {
        addr |= (hwaddr)-(1L << va_bits);
    }
#endif

    return addr;
}

#define PTE_HEADER_FIELDS       "vaddr            paddr            "\
                                "size             attr\n"
#define PTE_HEADER_DELIMITER    "---------------- ---------------- "\
                                "---------------- ------------------------------\n"

static void print_pte_header(Monitor *mon)
{
    monitor_printf(mon, PTE_HEADER_FIELDS);
    monitor_printf(mon, PTE_HEADER_DELIMITER);
}

static void
print_pte_lpae(Monitor *mon, uint32_t tableattrs, int va_bits,
               target_ulong vaddr, hwaddr paddr, target_ulong size,
               target_ulong pte)
{
    uint32_t ns = extract64(pte, 5, 1) | extract32(tableattrs, 4, 1);
    uint32_t ap = extract64(pte, 6, 2) & ~extract32(tableattrs, 2, 2);
    uint32_t af = extract64(pte, 10, 1);
    uint32_t ng = extract64(pte, 11, 1);
    uint32_t gp = extract64(pte, 50, 1);
    uint32_t con = extract64(pte, 52, 1);
    uint32_t pxn = extract64(pte, 53, 1) | extract32(tableattrs, 0, 1);
    uint32_t uxn = extract64(pte, 54, 1) | extract32(tableattrs, 1, 1);

    monitor_printf(mon, TARGET_FMT_lx " " TARGET_FMT_plx " " TARGET_FMT_lx
                   " %s %s %s %s %s %s %s %s %s\n",
                   addr_canonical(va_bits, vaddr), paddr, size,
                   ap & 0x2 ? "ro" : "RW",
                   ap & 0x1 ? "USR" : "   ",
                   ns ? "NS" : "  ",
                   af ? "AF" : "  ",
                   ng ? "nG" : "  ",
                   gp ? "GP" : "  ",
                   con ? "Con" : "   ",
                   pxn ? "PXN" : "   ",
                   uxn ? "UXN" : "   ");
}

static void
walk_pte_lpae(Monitor *mon, bool aarch64, uint32_t tableattrs, hwaddr pt_base,
              target_ulong vstart, int cur_level, int stride, int va_bits)
{
    int pg_shift = stride + 3;
    int descaddr_high = aarch64 ? 47 : 39;
    int max_level = 3;
    int ptshift = pg_shift + (max_level - cur_level) * stride;
    target_ulong pgsize = 1UL << ptshift;
    int idx;

    for (idx = 0; idx < (1UL << stride) && vstart < (1UL << va_bits);
         idx++, vstart += pgsize) {
        hwaddr pte_addr = pt_base + idx * 8;
        target_ulong pte = 0;
        hwaddr paddr;

        cpu_physical_memory_read(pte_addr, &pte, 8);

        if (!extract64(pte, 0, 1)) {
            /* invalid entry */
            continue;
        }

        if (cur_level == max_level) {
            /* leaf entry */
            paddr = (hwaddr)extract64(pte, pg_shift,
                                descaddr_high - pg_shift + 1) << pg_shift;
            print_pte_lpae(mon, tableattrs, va_bits, vstart, paddr,
                           pgsize, pte);
        } else {
            if (extract64(pte, 1, 1)) {
                /* table entry */
                paddr = (hwaddr)extract64(pte, pg_shift,
                                    descaddr_high - pg_shift + 1) << pg_shift;
                tableattrs |= extract64(pte, 59, 5);

                walk_pte_lpae(mon, aarch64, tableattrs, paddr, vstart,
                              cur_level + 1, stride, va_bits);
            } else {
                /* block entry */
                if ((pg_shift == 12 && (cur_level != 1 && cur_level != 2)) ||
                    (pg_shift == 14 && (cur_level != 2)) ||
                    (pg_shift == 16 && (cur_level != 0 && cur_level != 1))) {
                    monitor_printf(mon, "illegal block entry at level%d\n",
                                   cur_level);
                    continue;
                }
                paddr = (hwaddr)extract64(pte, ptshift,
                                    descaddr_high - ptshift + 1) << ptshift;
                print_pte_lpae(mon, tableattrs, va_bits, vstart, paddr,
                               pgsize, pte);
            }
        }
    }
}

/* ARMv8-A AArch64 Long Descriptor format */
static void tlb_info_vmsav8_64(Monitor *mon, CPUArchState *env)
{
    ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
    uint64_t ttbr[2];
    uint64_t tcr;
    int tsz[2];
    bool using16k, using64k;
    int stride;

    ttbr[0] = regime_ttbr(env, mmu_idx, 0);
    ttbr[1] = regime_ttbr(env, mmu_idx, 1);

    tcr = regime_tcr(env, mmu_idx)->raw_tcr;
    using64k = extract32(tcr, 14, 1);
    using16k = extract32(tcr, 15, 1);
    tsz[0] = extract32(tcr, 0, 6);
    tsz[1] = extract32(tcr, 16, 6);

    if (using64k) {
        stride = 13;
    } else if (using16k) {
        stride = 11;
    } else {
        stride = 9;
    }

    /* print header */
    print_pte_header(mon);

    for (unsigned int i = 0; i < 2; i++) {
        if (ttbr[i]) {
            hwaddr base = extract64(ttbr[i], 1, 47) << 1;
            int va_bits = 64 - tsz[i];
            target_ulong vstart = (target_ulong)i << (va_bits - 1);
            int startlevel = pt_start_level_stage1(va_bits, stride);

            /* walk ttbrx page tables, starting from address @vstart */
            walk_pte_lpae(mon, true, 0, base, vstart, startlevel,
                          stride, va_bits);
        }
    }
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (arm_feature(env, ARM_FEATURE_PMSA)) {
        monitor_printf(mon, "No MMU\n");
        return;
    }

    if (regime_translation_disabled(env, arm_stage1_mmu_idx(env))) {
        monitor_printf(mon, "MMU disabled\n");
        return;
    }

    if (!arm_el_is_aa64(env, 1)) {
        monitor_printf(mon, "Only AArch64 Long Descriptor is supported\n");
        return;
    }

    tlb_info_vmsav8_64(mon, env);
}
