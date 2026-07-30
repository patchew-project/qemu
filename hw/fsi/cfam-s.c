/*
 * IBM Common FRU Access Macro - S variant (CFAM-S)
 *
 * A CFAM flavor built on the common CFAM model (see cfam.c). It supports an
 * FSI responder and a v1 mailbox, and exposes its register slot in each
 * slave-ID view of the link.
 *
 * Copyright (C) 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/fsi/cfam-s.h"
#include "hw/fsi/fsi.h"

/* Slot layout: 0x000 config table, 0x400 responder, 0x800 mailbox */
#define CFAM_S_RESPONDER_BASE 0x400
#define CFAM_S_MBOX_BASE      0x800

/* Config table: chip ID with major 9 (the CFAM-S), then the engines */
static const uint32_t cfam_s_config[] = {
    ENGINE_CONFIG_NEXT | CFAM_CONFIG_CHIP_ID_MAJOR(9) | 0xd,
    CFAM_CONFIG_REG(0x1000, ENGINE_CONFIG_TYPE_FSI, 0xb),
    CFAM_CONFIG_LAST(0x1000, ENGINE_CONFIG_TYPE_MBOX_V1, 0x3),
};

static bool fsi_cfam_s_realize_engines(FSICFAMCommonState *cfam, Error **errp)
{
    FSICFAMSState *cfam_s = FSI_CFAM_S(cfam);

    object_initialize_child(OBJECT(cfam_s), "mbox", &cfam_s->mbox,
                            TYPE_FSI_MBOX);
    return fsi_cfam_add_engine(cfam, DEVICE(&cfam_s->mbox), 0, errp);
}

static void fsi_cfam_s_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    FSICFAMSState *cfam_s = FSI_CFAM_S(dev);
    FSICFAMCommonState *cfam = FSI_CFAM_COMMON(dev);
    FSICFAMSClass *sc = FSI_CFAM_S_GET_CLASS(dev);

    sc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    memory_region_init(&cfam_s->window, OBJECT(cfam_s),
                       TYPE_FSI_CFAM_S ".window", CFAM_S_WINDOW_SIZE);
    memory_region_add_subregion(&cfam_s->window, 0, &cfam->mr);

    /* Alias the slot into the other three slave-ID views */
    for (int i = 0; i < ARRAY_SIZE(cfam_s->slot_alias); i++) {
        memory_region_init_alias(&cfam_s->slot_alias[i], OBJECT(cfam_s),
                                 TYPE_FSI_CFAM_S ".slot-alias", &cfam->mr, 0,
                                 FSI_CFAM_SLOT_SIZE);
        memory_region_add_subregion(&cfam_s->window,
                                    (i + 1) * FSI_CFAM_SLOT_SIZE,
                                    &cfam_s->slot_alias[i]);
    }
}

static void fsi_cfam_s_class_init(ObjectClass *klass, const void *data)
{
    FSICFAMSClass *sc = FSI_CFAM_S_CLASS(klass);
    FSICFAMCommonClass *cc = FSI_CFAM_COMMON_CLASS(klass);

    device_class_set_parent_realize(DEVICE_CLASS(klass), fsi_cfam_s_realize,
                                    &sc->parent_realize);

    cc->config = cfam_s_config;
    cc->config_nr = ARRAY_SIZE(cfam_s_config);
    cc->responder_offset = CFAM_S_RESPONDER_BASE;
    cc->lbus_offset = CFAM_S_MBOX_BASE;
    cc->realize_engines = fsi_cfam_s_realize_engines;
}

static const TypeInfo cfam_s_types[] = {
    {
        .name = TYPE_FSI_CFAM_S,
        .parent = TYPE_FSI_CFAM_COMMON,
        .instance_size = sizeof(FSICFAMSState),
        .class_size = sizeof(FSICFAMSClass),
        .class_init = fsi_cfam_s_class_init,
    },
};

DEFINE_TYPES(cfam_s_types)
