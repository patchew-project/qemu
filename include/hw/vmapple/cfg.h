/*
 * VMApple Configuration Region
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VMAPPLE_CFG_H
#define HW_VMAPPLE_CFG_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "net/net.h"

typedef struct VMAppleCfg {
    uint32_t version;         /* 0x000 */
    uint32_t nr_cpus;         /* 0x004 */
    uint32_t unk1;            /* 0x008 */
    uint32_t unk2;            /* 0x00c */
    uint32_t unk3;            /* 0x010 */
    uint32_t unk4;            /* 0x014 */
    uint64_t ecid;            /* 0x018 */
    uint64_t ram_size;        /* 0x020 */
    uint32_t run_installer1;  /* 0x028 */
    uint32_t unk5;            /* 0x02c */
    uint32_t unk6;            /* 0x030 */
    uint32_t run_installer2;  /* 0x034 */
    uint32_t rnd;             /* 0x038 */
    uint32_t unk7;            /* 0x03c */
    MACAddr mac_en0;          /* 0x040 */
    uint8_t pad1[2];
    MACAddr mac_en1;          /* 0x048 */
    uint8_t pad2[2];
    MACAddr mac_wifi0;        /* 0x050 */
    uint8_t pad3[2];
    MACAddr mac_bt0;          /* 0x058 */
    uint8_t pad4[2];
    uint8_t reserved[0xa0];   /* 0x060 */
    uint32_t cpu_ids[0x80];   /* 0x100 */
    uint8_t scratch[0x200];   /* 0x180 */
    char serial[32];          /* 0x380 */
    char unk8[32];            /* 0x3a0 */
    char model[32];           /* 0x3c0 */
    uint8_t unk9[32];         /* 0x3e0 */
    uint32_t unk10;           /* 0x400 */
    char soc_name[32];        /* 0x404 */
} VMAppleCfg;

#define TYPE_VMAPPLE_CFG "vmapple-cfg"
OBJECT_DECLARE_SIMPLE_TYPE(VMAppleCfgState, VMAPPLE_CFG)

struct VMAppleCfgState {
    /* <private> */
    SysBusDevice parent_obj;
    VMAppleCfg cfg;

    /* <public> */
    MemoryRegion mem;
    char *serial;
    char *model;
    char *soc_name;
};

#define VMAPPLE_CFG_SIZE 0x00010000

#endif /* HW_VMAPPLE_CFG_H */
