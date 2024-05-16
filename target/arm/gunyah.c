/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "linux-headers/linux/gunyah.h"

/*
 * Specify location of device-tree in guest address space.
 *
 * @dtb_start - Guest physical address where VM's device-tree is found
 * @dtb_size - Size of device-tree (and any free space after it).
 *
 * RM or Resource Manager VM is a trusted and privileged VM that works in
 * collaboration with Gunyah hypevisor to setup resources for a VM before it can
 * begin execution. One of its functions includes inspection/modification of a
 * VM's device-tree before VM begins its execution. Modification can
 * include specification of runtime resources allocated by hypervisor,
 * details of which needs to be visible to VM.  VM's device-tree is modified
 * "inline" making use of "free" space that could exist at the end of device
 * tree.
 */
int gunyah_arm_set_dtb(uint64_t dtb_start, uint64_t dtb_size)
{
    int ret;
    struct gh_vm_dtb_config dtb;

    dtb.guest_phys_addr = dtb_start;
    dtb.size = dtb_size;

    ret = gunyah_vm_ioctl(GH_VM_SET_DTB_CONFIG, &dtb);
    if (ret != 0) {
        error_report("GH_VM_SET_DTB_CONFIG failed: %s", strerror(errno));
        exit(1);
    }

    return 0;
}
