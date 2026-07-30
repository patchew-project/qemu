/*
 * IBM Common FRU Access Macro - S variant (CFAM-S)
 *
 * Copyright (C) 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FSI_CFAM_S_H
#define FSI_CFAM_S_H

#include "system/memory.h"
#include "hw/fsi/cfam.h"
#include "hw/fsi/fsi.h"
#include "hw/fsi/lbus.h"

#define TYPE_FSI_CFAM_S "cfam-s"
OBJECT_DECLARE_TYPE(FSICFAMSState, FSICFAMSClass, FSI_CFAM_S)

/* The register slot is visible in each of the four slave-ID views */
#define CFAM_S_WINDOW_SIZE (4 * FSI_CFAM_SLOT_SIZE)

struct FSICFAMSState {
    /* < private > */
    FSICFAMCommonState parent;

    /* parent.mr aliased across the slave-ID views */
    MemoryRegion window;
    MemoryRegion slot_alias[3];

    FSIMbox mbox;
};

struct FSICFAMSClass {
    /* < private > */
    FSICFAMCommonClass parent_class;

    DeviceRealize parent_realize;
};

#endif /* FSI_CFAM_S_H */
