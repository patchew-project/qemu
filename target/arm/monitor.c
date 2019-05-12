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
#include "qapi/qapi-commands-target.h"
#include "monitor/hmp-target.h"

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

#ifdef CONFIG_KVM
static SVEVectorLengths *qmp_kvm_sve_vls_get(void)
{
    CPUArchState *env = mon_get_cpu_env();
    ARMCPU *cpu = arm_env_get_cpu(env);
    uint64_t sve_vls[KVM_ARM64_SVE_VLS_WORDS];
    SVEVectorLengths *vls = g_new(SVEVectorLengths, 1);
    intList **v = &vls->vls;
    int ret, i;

    ret = kvm_arm_get_sve_vls(CPU(cpu), sve_vls);
    if (ret <= 0) {
        *v = g_new0(intList, 1); /* one vl of 0 means none supported */
        return vls;
    }

    for (i = KVM_ARM64_SVE_VQ_MIN; i <= ret; ++i) {
        int bitval = (sve_vls[(i - KVM_ARM64_SVE_VQ_MIN) / 64] >>
                      ((i - KVM_ARM64_SVE_VQ_MIN) % 64)) & 1;
        if (bitval) {
            *v = g_new0(intList, 1);
            (*v)->value = i;
            v = &(*v)->next;
        }
    }

    return vls;
}
#else
static SVEVectorLengths *qmp_kvm_sve_vls_get(void)
{
    return NULL;
}
#endif

static SVEVectorLengths *qmp_sve_vls_get(void)
{
    CPUArchState *env = mon_get_cpu_env();
    ARMCPU *cpu = arm_env_get_cpu(env);
    SVEVectorLengths *vls = g_new(SVEVectorLengths, 1);
    intList **v = &vls->vls;
    int i;

    if (cpu->sve_max_vq == 0) {
        *v = g_new0(intList, 1); /* one vl of 0 means none supported */
        return vls;
    }

    for (i = 1; i <= cpu->sve_max_vq; ++i) {
        int bitval = (cpu->sve_vls_map >> (i - 1)) & 1;
        if (bitval) {
            *v = g_new0(intList, 1);
            (*v)->value = i;
            v = &(*v)->next;
        }
    }

    return vls;
}

static SVEVectorLengths *qmp_sve_vls_dup_and_truncate(SVEVectorLengths *vls)
{
    SVEVectorLengths *trunc_vls;
    intList **v, *p = vls->vls;

    if (!p->next) {
        return NULL;
    }

    trunc_vls = g_new(SVEVectorLengths, 1);
    v = &trunc_vls->vls;

    for (; p->next; p = p->next) {
        *v = g_new0(intList, 1);
        (*v)->value = p->value;
        v = &(*v)->next;
    }

    return trunc_vls;
}

SVEVectorLengthsList *qmp_query_sve_vector_lengths(Error **errp)
{
    SVEVectorLengthsList *vls_list = g_new0(SVEVectorLengthsList, 1);
    SVEVectorLengths *vls;

    if (kvm_enabled()) {
        vls = qmp_kvm_sve_vls_get();
    } else {
        vls = qmp_sve_vls_get();
    }

    while (vls) {
        vls_list->value = vls;
        vls = qmp_sve_vls_dup_and_truncate(vls);
        if (vls) {
            SVEVectorLengthsList *next = vls_list;
            vls_list = g_new0(SVEVectorLengthsList, 1);
            vls_list->next = next;
        }
    }

    return vls_list;
}
