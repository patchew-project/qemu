/*
 * Secure execution functions
 *
 * Copyright IBM Corp. 2019
 * Author(s):
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include <linux/kvm.h>

#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "pv.h"

const char* cmd_names[] = {
    "VM_CREATE",
    "VM_DESTROY",
    "VM_SET_SEC_PARAMS",
    "VM_UNPACK",
    "VM_VERIFY",
    "VM_PREP_RESET",
    "VM_UNSHARE_ALL",
    "VCPU_CREATE",
    "VCPU_DESTROY",
    "VCPU_SET_IPL_PSW",
    NULL
};

static int s390_pv_cmd(uint32_t cmd, void *data)
{
    int rc;
    struct kvm_pv_cmd pv_cmd = {
        .cmd = cmd,
        .data = (uint64_t)data,
    };

    rc = kvm_vm_ioctl(kvm_state, KVM_S390_PV_COMMAND, &pv_cmd);
    if (rc) {
        error_report("KVM PV command %d (%s) failed: header rc %x rrc %x "
                     "IOCTL rc: %d", cmd, cmd_names[cmd], pv_cmd.rc, pv_cmd.rrc,
                     rc);
    }
    return rc;
}

static void s390_pv_cmd_exit(uint32_t cmd, void *data)
{
    int rc;

    rc = s390_pv_cmd(cmd, data);
    if (rc) {
        exit(1);
    }
}

static int s390_pv_cmd_vcpu(CPUState *cs, uint32_t cmd, void *data)
{
    int rc;
    struct kvm_pv_cmd pv_cmd = {
        .cmd = cmd,
        .data = (uint64_t)data,
    };

    rc = kvm_vcpu_ioctl(cs, KVM_S390_PV_COMMAND_VCPU, &pv_cmd);
    if (rc) {
        error_report("KVM PV VCPU command %d (%s) failed header: rc %x rrc %x "
                     "IOCTL rc: %d", cmd, cmd_names[cmd], pv_cmd.rc, pv_cmd.rrc,
                     rc);
    }
    return rc;
}

static void s390_pv_cmd_vcpu_exit(CPUState *cs, uint32_t cmd, void *data)
{
    int rc;

    rc = s390_pv_cmd_vcpu(cs, cmd, data);
    if (rc) {
        exit(1);
    }

}

int s390_pv_vm_create(void)
{
    return s390_pv_cmd(KVM_PV_VM_CREATE, NULL);
}

void s390_pv_vm_destroy(void)
{
     s390_pv_cmd_exit(KVM_PV_VM_DESTROY, NULL);
}

int s390_pv_vcpu_create(CPUState *cs)
{
    int rc;

    rc = s390_pv_cmd_vcpu(cs, KVM_PV_VCPU_CREATE, NULL);
    if (!rc) {
        S390_CPU(cs)->env.pv = true;
    }

    return rc;
}

void s390_pv_vcpu_destroy(CPUState *cs)
{
    s390_pv_cmd_vcpu_exit(cs, KVM_PV_VCPU_DESTROY, NULL);
    S390_CPU(cs)->env.pv = false;
}

int s390_pv_set_sec_parms(uint64_t origin, uint64_t length)
{
    struct kvm_s390_pv_sec_parm args = {
        .origin = origin,
        .length = length,
    };

    return s390_pv_cmd(KVM_PV_VM_SET_SEC_PARMS, &args);
}

void s390_pv_set_ipl_psw(CPUState *cs)
{
    s390_pv_cmd_vcpu_exit(cs, KVM_PV_VCPU_SET_IPL_PSW, NULL);
}

/*
 * Called for each component in the SE type IPL parameter block 0.
 */
int s390_pv_unpack(uint64_t addr, uint64_t size, uint64_t tweak)
{
    struct kvm_s390_pv_unp args = {
        .addr = addr,
        .size = size,
        .tweak = tweak,
    };

    return s390_pv_cmd(KVM_PV_VM_UNPACK, &args);
}

void s390_pv_perf_clear_reset(void)
{
    s390_pv_cmd_exit(KVM_PV_VM_PREP_RESET, NULL);
}

int s390_pv_verify(void)
{
    return s390_pv_cmd(KVM_PV_VM_VERIFY, NULL);
}

void s390_pv_unshare(void)
{
    s390_pv_cmd_exit(KVM_PV_VM_UNSHARE_ALL, NULL);
}
