/*
 * Beckhoff Communication Controller Emulation
 *
 * Copyright (c) Beckhoff Automation GmbH. & Co. KG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "system/dma.h"
#include "qemu/error-report.h"
#include "block/block.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "hw/block/block.h"
#include "migration/vmstate.h"
#include "qemu/bswap.h"

#ifndef CCAT_ERR_DEBUG
#define CCAT_ERR_DEBUG 0
#endif

#define TYPE_BECKHOFF_CCAT "beckhoff-ccat"
OBJECT_DECLARE_SIMPLE_TYPE(BeckhoffCcat, BECKHOFF_CCAT)

#define MAX_NUM_SLOTS 32
#define CCAT_FUNCTION_BLOCK_SIZE 16

#define CCAT_EEPROM_OFFSET 0x100
#define CCAT_DMA_OFFSET 0x8000

#define CCAT_MEM_SIZE 0xFFFF
#define CCAT_DMA_SIZE 0x800
#define CCAT_EEPROM_SIZE 0x20

#define EEPROM_MEMORY_SIZE 0x1000

#define EEPROM_CMD_OFFSET (CCAT_EEPROM_OFFSET + 0x00)
    #define EEPROM_CMD_WRITE_MASK 0x2
    #define EEPROM_CMD_READ_MASK 0x1
#define EEPROM_ADR_OFFSET (CCAT_EEPROM_OFFSET + 0x04)
#define EEPROM_DATA_OFFSET (CCAT_EEPROM_OFFSET + 0x08)

#define DMA_BUFFER_OFFSET (CCAT_DMA_OFFSET + 0x00)
#define DMA_DIRECTION_OFFSET (CCAT_DMA_OFFSET + 0x7c0)
    #define DMA_DIRECTION_MASK 1
#define DMA_TRANSFER_OFFSET (CCAT_DMA_OFFSET + 0x7c4)
#define DMA_HOST_ADR_OFFSET (CCAT_DMA_OFFSET + 0x7c8)
#define DMA_TRANSFER_LENGTH_OFFSET (CCAT_DMA_OFFSET + 0x7cc)

/*
 * The informationblock  is always located at address 0x0.
 * Address and size are therefor replaced by two identifiers.
 * The Parameter give information about the maximal number of
 * function slots and the creation date (in this case 01.01.2001)
 */
#define CCAT_ID_1 0x88a4
#define CCAT_ID_2 0x54414343
#define CCAT_INFO_BLOCK_PARAMS ((MAX_NUM_SLOTS << 0) | (0x1 << 8) | \
                              (0x1 << 16) | (0x1 << 24))

#define CCAT_FUN_TYPE_ENTRY 0x0001
#define CCAT_FUN_TYPE_EEPROM 0x0012
#define CCAT_FUN_TYPE_DMA 0x0013

typedef struct BeckhoffCcat {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint8_t mem[CCAT_MEM_SIZE];

    BlockBackend *eeprom_blk;
    uint8_t *eeprom_storage;
    uint32_t eeprom_size;
} BeckhoffCcat;

static void sync_eeprom(BeckhoffCcat *s)
{
    if (!s->eeprom_blk) {
        return;
    }
    blk_pwrite(s->eeprom_blk, 0, s->eeprom_size, s->eeprom_storage, 0);
}

static uint64_t beckhoff_ccat_eeprom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    BeckhoffCcat *s = opaque;
    return ldn_le_p(&s->mem[addr], size);
}

static void beckhoff_ccat_eeprom_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    BeckhoffCcat *s = opaque;
    uint64_t eeprom_adr;
    uint64_t buf;
    uint32_t bytes_to_read;

    switch (addr) {
    case EEPROM_CMD_OFFSET:
        eeprom_adr = ldl_le_p(&s->mem[EEPROM_ADR_OFFSET]);
        eeprom_adr = (eeprom_adr * 2) % s->eeprom_size;
        if (val & EEPROM_CMD_READ_MASK) {
            buf = 0;
            bytes_to_read = 8;
            if (eeprom_adr > s->eeprom_size - 8) {
                bytes_to_read = s->eeprom_size - eeprom_adr;
            }
            buf = ldn_le_p(s->eeprom_storage + eeprom_adr, bytes_to_read);
            stq_le_p(&s->mem[EEPROM_DATA_OFFSET], buf);
        } else if (val & EEPROM_CMD_WRITE_MASK) {
            buf = ldl_le_p(&s->mem[EEPROM_DATA_OFFSET]);
            stw_le_p((uint16_t *)(s->eeprom_storage + eeprom_adr), buf);
            sync_eeprom(s);
        }
        break;
    default:
        stn_le_p(&s->mem[addr], size, val);
    }
}

static uint64_t beckhoff_ccat_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    BeckhoffCcat *s = opaque;

    switch (addr) {
    case DMA_TRANSFER_OFFSET:
        if (s->mem[DMA_TRANSFER_OFFSET] & 0x1) {
            s->mem[DMA_TRANSFER_OFFSET] = 0;
        }
        break;
    }
    return ldn_le_p(&s->mem[addr], size);
}

static void beckhoff_ccat_dma_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    BeckhoffCcat *s = opaque;
    dma_addr_t dmaAddr;
    uint8_t len;
    uint8_t *mem_buf;

    switch (addr) {
    case DMA_TRANSFER_OFFSET:
        len = s->mem[DMA_TRANSFER_LENGTH_OFFSET];
        mem_buf = &s->mem[DMA_BUFFER_OFFSET];
        dmaAddr = ldl_le_p(&s->mem[DMA_HOST_ADR_OFFSET]);
        if (s->mem[DMA_DIRECTION_OFFSET] & DMA_DIRECTION_MASK) {
            dma_memory_read(&address_space_memory, dmaAddr,
                            mem_buf, len * 8, MEMTXATTRS_UNSPECIFIED);
        } else {
            /*
             * The write transfer uses Host DMA Address + 8 as the target
             * offset, as described in the CCAT manual Version 0.0.41
             * section 20.2.
             */
            dma_memory_write(&address_space_memory, dmaAddr + 8,
                                mem_buf, len * 8, MEMTXATTRS_UNSPECIFIED);
        }
        break;
    }
    stn_le_p(&s->mem[addr], size, val);
}

static uint64_t beckhoff_ccat_read(void *opaque, hwaddr addr, unsigned size)
{
    BeckhoffCcat *s = opaque;
    uint64_t val = 0;

    assert(addr <= CCAT_MEM_SIZE - size);

    if (addr >= CCAT_EEPROM_OFFSET &&
                        addr <= CCAT_EEPROM_OFFSET + s->eeprom_size) {
        return beckhoff_ccat_eeprom_read(opaque, addr, size);
    } else if (addr >= CCAT_DMA_OFFSET &&
                        addr <= CCAT_DMA_OFFSET + CCAT_DMA_SIZE) {
        return beckhoff_ccat_dma_read(opaque, addr, size);
    } else {
        val = ldn_le_p(&s->mem[addr], size);
    }

    return val;
}

static void beckhoff_ccat_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    BeckhoffCcat *s = opaque;

    assert(addr <= CCAT_MEM_SIZE - size);

    if (addr >= CCAT_EEPROM_OFFSET &&
                        addr <= CCAT_EEPROM_OFFSET + s->eeprom_size) {
        beckhoff_ccat_eeprom_write(opaque, addr, val, size);
    } else if (addr >= CCAT_DMA_OFFSET &&
                        addr <= CCAT_DMA_OFFSET + CCAT_DMA_SIZE) {
        beckhoff_ccat_dma_write(opaque, addr, val, size);
    } else {
        stn_le_p(&s->mem[addr], size, val);
    }
}

static const MemoryRegionOps beckhoff_ccat_ops = {
    .read = beckhoff_ccat_read,
    .write = beckhoff_ccat_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void beckhoff_ccat_reset(DeviceState *dev)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(dev);

    memset(&s->mem[0], 0, MAX_NUM_SLOTS * CCAT_FUNCTION_BLOCK_SIZE);

    size_t offset = 0 * CCAT_FUNCTION_BLOCK_SIZE;
    stw_le_p(&s->mem[offset + 0], CCAT_FUN_TYPE_ENTRY);
    stw_le_p(&s->mem[offset + 2], 0x0001);
    stl_le_p(&s->mem[offset + 4], CCAT_INFO_BLOCK_PARAMS);
    stl_le_p(&s->mem[offset + 8], CCAT_ID_1);
    stl_le_p(&s->mem[offset + 12], CCAT_ID_2);

    offset = 11 * CCAT_FUNCTION_BLOCK_SIZE;
    stw_le_p(&s->mem[offset + 0], CCAT_FUN_TYPE_EEPROM);
    stw_le_p(&s->mem[offset + 2], 0x0001);
    stl_le_p(&s->mem[offset + 4], 0);
    stl_le_p(&s->mem[offset + 8], CCAT_EEPROM_OFFSET);
    stl_le_p(&s->mem[offset + 12], CCAT_EEPROM_SIZE);

    offset = 15 * CCAT_FUNCTION_BLOCK_SIZE;
    stw_le_p(&s->mem[offset + 0], CCAT_FUN_TYPE_DMA);
    stw_le_p(&s->mem[offset + 2], 0x0000);
    stl_le_p(&s->mem[offset + 4], 0);
    stl_le_p(&s->mem[offset + 8], CCAT_DMA_OFFSET);
    stl_le_p(&s->mem[offset + 12], CCAT_DMA_SIZE);
}

static void beckhoff_ccat_realize(DeviceState *dev, Error **errp)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(dev);
    BlockBackend *blk;

    blk = s->eeprom_blk;

    if (blk) {
        uint64_t blk_size = blk_getlength(blk);
        if (!is_power_of_2(blk_size)) {
            error_setg(errp, "Blockend size is not a power of two.");
            return;
        }

        if (blk_size < 512) {
            error_setg(errp, "Blockend size is too small.");
            return;
        } else {
            blk_set_perm(blk, BLK_PERM_WRITE, BLK_PERM_ALL, errp);

            s->eeprom_size = blk_size;
            s->eeprom_blk = blk;
            s->eeprom_storage = blk_blockalign(s->eeprom_blk, s->eeprom_size);

            if (!blk_check_size_and_read_all(s->eeprom_blk, DEVICE(s),
                                             s->eeprom_storage, s->eeprom_size,
                                             errp)) {
                return;
            }
        }
    } else {
        s->eeprom_size = EEPROM_MEMORY_SIZE;
        s->eeprom_storage = blk_blockalign(NULL, s->eeprom_size);
        memset(s->eeprom_storage, 0x00, s->eeprom_size);
    }
}

static void beckhoff_ccat_init(Object *obj)
{
    BeckhoffCcat *s = BECKHOFF_CCAT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &beckhoff_ccat_ops, s,
                          TYPE_BECKHOFF_CCAT, CCAT_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_beckhoff_ccat = {
    .name = "beckhoff-ccat",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(mem, BeckhoffCcat, CCAT_MEM_SIZE),
        VMSTATE_UINT32(eeprom_size, BeckhoffCcat),
        VMSTATE_VBUFFER_UINT32(eeprom_storage, BeckhoffCcat, 1, NULL,
                               eeprom_size),
        VMSTATE_END_OF_LIST()
    }
};

static const Property beckhoff_ccat_properties[] = {
    DEFINE_PROP_DRIVE("eeprom", BeckhoffCcat, eeprom_blk),
};

static void beckhoff_ccat_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = beckhoff_ccat_realize;
    device_class_set_legacy_reset(dc, beckhoff_ccat_reset);
    dc->vmsd = &vmstate_beckhoff_ccat;
    device_class_set_props(dc, beckhoff_ccat_properties);
}

static const TypeInfo beckhoff_ccat_info = {
 .name = TYPE_BECKHOFF_CCAT,
 .parent = TYPE_SYS_BUS_DEVICE,
 .instance_size = sizeof(BeckhoffCcat),
 .instance_init = beckhoff_ccat_init,
 .class_init = beckhoff_ccat_class_init,
};

static void beckhoff_ccat_register_types(void)
{
    type_register_static(&beckhoff_ccat_info);
}

type_init(beckhoff_ccat_register_types)
