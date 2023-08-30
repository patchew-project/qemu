/*
 * VMApple Backdoor Interface
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vmapple/bdif.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/block/block.h"
#include "sysemu/block-backend.h"

#define REG_DEVID_MASK      0xffff0000
#define DEVID_ROOT          0x00000000
#define DEVID_AUX           0x00010000
#define DEVID_USB           0x00100000

#define REG_STATUS          0x0
#define REG_STATUS_ACTIVE     BIT(0)
#define REG_CFG             0x4
#define REG_CFG_ACTIVE        BIT(1)
#define REG_UNK1            0x8
#define REG_BUSY            0x10
#define REG_BUSY_READY        BIT(0)
#define REG_UNK2            0x400
#define REG_CMD             0x408
#define REG_NEXT_DEVICE     0x420
#define REG_UNK3            0x434

typedef struct vblk_sector {
    uint32_t pad;
    uint32_t pad2;
    uint32_t sector;
    uint32_t pad3;
} VblkSector;

typedef struct vblk_req_cmd {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} VblkReqCmd;

typedef struct vblk_req {
    VblkReqCmd sector;
    VblkReqCmd data;
    VblkReqCmd retval;
} VblkReq;

#define VBLK_DATA_FLAGS_READ  0x00030001
#define VBLK_DATA_FLAGS_WRITE 0x00010001

#define VBLK_RET_SUCCESS  0
#define VBLK_RET_FAILED   1

static uint64_t bdif_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t ret = -1;
    uint64_t devid = (offset & REG_DEVID_MASK);

    switch (offset & ~REG_DEVID_MASK) {
    case REG_STATUS:
        ret = REG_STATUS_ACTIVE;
        break;
    case REG_CFG:
        ret = REG_CFG_ACTIVE;
        break;
    case REG_UNK1:
        ret = 0x420;
        break;
    case REG_BUSY:
        ret = REG_BUSY_READY;
        break;
    case REG_UNK2:
        ret = 0x1;
        break;
    case REG_UNK3:
        ret = 0x0;
        break;
    case REG_NEXT_DEVICE:
        switch (devid) {
        case DEVID_ROOT:
            ret = 0x8000000;
            break;
        case DEVID_AUX:
            ret = 0x10000;
            break;
        }
        break;
    }

    trace_bdif_read(offset, size, ret);
    return ret;
}

static void le2cpu_sector(VblkSector *sector)
{
    sector->sector = le32_to_cpu(sector->sector);
}

static void le2cpu_reqcmd(VblkReqCmd *cmd)
{
    cmd->addr = le64_to_cpu(cmd->addr);
    cmd->len = le32_to_cpu(cmd->len);
    cmd->flags = le32_to_cpu(cmd->flags);
}

static void le2cpu_req(VblkReq *req)
{
    le2cpu_reqcmd(&req->sector);
    le2cpu_reqcmd(&req->data);
    le2cpu_reqcmd(&req->retval);
}

static void vblk_cmd(uint64_t devid, BlockBackend *blk, uint64_t value,
                     uint64_t static_off)
{
    VblkReq req;
    VblkSector sector;
    uint64_t off = 0;
    char *buf = NULL;
    uint8_t ret = VBLK_RET_FAILED;
    int r;

    cpu_physical_memory_read(value, &req, sizeof(req));
    le2cpu_req(&req);

    if (req.sector.len != sizeof(sector)) {
        ret = VBLK_RET_FAILED;
        goto out;
    }

    /* Read the vblk command */
    cpu_physical_memory_read(req.sector.addr, &sector, sizeof(sector));
    le2cpu_sector(&sector);

    off = sector.sector * 512ULL + static_off;

    /* Sanity check that we're not allocating bogus sizes */
    if (req.data.len > (128 * 1024 * 1024)) {
        goto out;
    }

    buf = g_malloc0(req.data.len);
    switch (req.data.flags) {
    case VBLK_DATA_FLAGS_READ:
        r = blk_pread(blk, off, req.data.len, buf, 0);
        trace_bdif_vblk_read(devid == DEVID_AUX ? "aux" : "root",
                             req.data.addr, off, req.data.len, r);
        if (r < 0) {
            goto out;
        }
        cpu_physical_memory_write(req.data.addr, buf, req.data.len);
        ret = VBLK_RET_SUCCESS;
        break;
    case VBLK_DATA_FLAGS_WRITE:
        /* Not needed, iBoot only reads */
        break;
    default:
        break;
    }

out:
    g_free(buf);
    cpu_physical_memory_write(req.retval.addr, &ret, 1);
}

static void bdif_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{
    VMAppleBdifState *s = opaque;
    uint64_t devid = (offset & REG_DEVID_MASK);

    trace_bdif_write(offset, size, value);

    switch (offset & ~REG_DEVID_MASK) {
    case REG_CMD:
        switch (devid) {
        case DEVID_ROOT:
            vblk_cmd(devid, s->root, value, 0x0);
            break;
        case DEVID_AUX:
            vblk_cmd(devid, s->aux, value, 0x0);
            break;
        }
        break;
    }
}

static const MemoryRegionOps bdif_ops = {
    .read = bdif_read,
    .write = bdif_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void bdif_init(Object *obj)
{
    VMAppleBdifState *s = VMAPPLE_BDIF(obj);

    memory_region_init_io(&s->mmio, obj, &bdif_ops, obj,
                         "VMApple Backdoor Interface", VMAPPLE_BDIF_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property bdif_properties[] = {
    DEFINE_PROP_DRIVE("aux", VMAppleBdifState, aux),
    DEFINE_PROP_DRIVE("root", VMAppleBdifState, root),
    DEFINE_PROP_END_OF_LIST(),
};

static void bdif_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "VMApple Backdoor Interface";
    device_class_set_props(dc, bdif_properties);
}

static const TypeInfo bdif_info = {
    .name          = TYPE_VMAPPLE_BDIF,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMAppleBdifState),
    .instance_init = bdif_init,
    .class_init    = bdif_class_init,
};

static void bdif_register_types(void)
{
    type_register_static(&bdif_info);
}

type_init(bdif_register_types)
