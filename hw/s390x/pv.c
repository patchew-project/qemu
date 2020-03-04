/*
 * Secure execution functions
 *
 * Copyright IBM Corp. 2020
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

const char *cmd_names[] = {
    "VM_ENABLE",
    "VM_DISABLE",
    "VM_SET_SEC_PARAMS",
    "VM_UNPACK",
    "VM_VERIFY",
    "VM_PREP_RESET",
    "VM_UNSHARE_ALL",
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

int s390_pv_vm_enable(void)
{
    return s390_pv_cmd(KVM_PV_ENABLE, NULL);
}

void s390_pv_vm_disable(void)
{
     s390_pv_cmd_exit(KVM_PV_DISABLE, NULL);
}

int s390_pv_set_sec_parms(uint64_t origin, uint64_t length)
{
    struct kvm_s390_pv_sec_parm args = {
        .origin = origin,
        .length = length,
    };

    return s390_pv_cmd(KVM_PV_VM_SET_SEC_PARMS, &args);
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
