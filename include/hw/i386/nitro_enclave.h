/*
 * AWS nitro-enclave machine
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef HW_I386_NITRO_ENCLAVE_H
#define HW_I386_NITRO_ENCLAVE_H

#include "hw/boards.h"
#include "hw/i386/microvm.h"
#include "qom/object.h"

/* Machine type options */
#define NITRO_ENCLAVE_GUEST_CID  "guest-cid"

struct NitroEnclaveMachineClass {
    MicrovmMachineClass parent;

    void (*parent_init)(MachineState *state);
};

struct NitroEnclaveMachineState {
    MicrovmMachineState parent;

    /* Machine type options */
    uint32_t guest_cid;
};

#define TYPE_NITRO_ENCLAVE_MACHINE MACHINE_TYPE_NAME("nitro-enclave")
OBJECT_DECLARE_TYPE(NitroEnclaveMachineState, NitroEnclaveMachineClass,
                    NITRO_ENCLAVE_MACHINE)

#endif
