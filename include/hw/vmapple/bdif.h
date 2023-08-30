/*
 * VMApple Backdoor Interface
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VMAPPLE_BDIF_H
#define HW_VMAPPLE_BDIF_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_VMAPPLE_BDIF "vmapple-bdif"
OBJECT_DECLARE_SIMPLE_TYPE(VMAppleBdifState, VMAPPLE_BDIF)

struct VMAppleBdifState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    BlockBackend *aux;
    BlockBackend *root;
    MemoryRegion mmio;
};

#define VMAPPLE_BDIF_SIZE 0x00200000

#endif /* HW_VMAPPLE_BDIF_H */
