/*
 * A sparse memory device
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"

#define TYPE_SPARSE_MEM "sparse-mem"
#define SPARSE_MEM(obj) OBJECT_CHECK(SparseMemState, (obj), TYPE_SPARSE_MEM)

#define SPARSE_BLOCK_SIZE 0x1000

typedef struct SparseMemState {
    DeviceState parent_obj;
    MemoryRegion mmio;
    uint64_t baseaddr;
    uint64_t length;
    uint64_t usage;
    uint64_t maxsize;
    GHashTable *mapped;
} SparseMemState;

typedef struct sparse_mem_block {
    uint16_t nonzeros;
    uint8_t data[SPARSE_BLOCK_SIZE];
} sparse_mem_block;

static uint64_t sparse_mem_read(void *opaque, hwaddr addr, unsigned int size)
{
    SparseMemState *s = opaque;
    uint64_t ret = 0;
    size_t pfn = addr / SPARSE_BLOCK_SIZE;
    size_t offset = addr % SPARSE_BLOCK_SIZE;
    sparse_mem_block *block;

    block = g_hash_table_lookup(s->mapped, (void *)pfn);
    if (block) {
        assert(offset + size <= sizeof(block->data));
        memcpy(&ret, block->data + offset, size);
    }
    return ret;
}

static void sparse_mem_write(void *opaque, hwaddr addr, uint64_t v,
                             unsigned int size)
{
    SparseMemState *s = opaque;
    size_t pfn = addr / SPARSE_BLOCK_SIZE;
    size_t offset = addr % SPARSE_BLOCK_SIZE;
    int nonzeros = 0;
    sparse_mem_block *block;

    if (!g_hash_table_lookup(s->mapped, (void *)pfn) &&
        s->usage + SPARSE_BLOCK_SIZE < s->maxsize && v) {
        g_hash_table_insert(s->mapped, (void *)pfn,
                            g_new0(sparse_mem_block, 1));
        s->usage += sizeof(block->data);
    }
    block = g_hash_table_lookup(s->mapped, (void *)pfn);
    if (!block) {
        return;
    }

    assert(offset + size <= sizeof(block->data));

    /*
     * Track the number of nonzeros, so we can adjust the block's nonzero count
     * after writing the value v
     */
    for (int i = 0; i < size; i++) {
        nonzeros -= (block->data[offset + i] != 0);
    }

    memcpy(block->data + offset, &v, size);

    for (int i = 0; i < size; i++) {
        nonzeros += (block->data[offset + i] != 0);
    }

    /* Update the number of nonzeros in the block, free it, if it's empty */
    assert(block->nonzeros + nonzeros < sizeof(block->data));
    assert((int)block->nonzeros + nonzeros >= 0);
    block->nonzeros += nonzeros;

    if (block->nonzeros == 0) {
        g_free(block);
        g_hash_table_remove(s->mapped, (void *)pfn);
        s->usage -= sizeof(block->data);
    }
}

static const MemoryRegionOps sparse_mem_ops = {
    .read = sparse_mem_read,
    .write = sparse_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 8,
            .unaligned = false,
        },
};

static Property sparse_mem_properties[] = {
    /* The base address of the memory */
    DEFINE_PROP_UINT64("baseaddr", SparseMemState, baseaddr, 0x0),
    /* The length of the sparse memory region */
    DEFINE_PROP_UINT64("length", SparseMemState, length, UINT64_MAX),
    /* Max amount of actual memory that can be used to back the sparse memory */
    DEFINE_PROP_UINT64("maxsize", SparseMemState, maxsize, 0x100000),
    DEFINE_PROP_END_OF_LIST(),
};

static void sparse_mem_realize(DeviceState *dev, Error **errp)
{
    SparseMemState *s = SPARSE_MEM(dev);

    assert(s->baseaddr + s->length > s->baseaddr);

    s->mapped = g_hash_table_new(NULL, NULL);
    memory_region_init_io(&(s->mmio), OBJECT(s), &sparse_mem_ops, s,
                          "sparse-mem", s->length);
    memory_region_add_subregion_overlap(get_system_memory(), s->baseaddr,
                                        &(s->mmio), -100);
}

static void sparse_mem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sparse_mem_properties);

    dc->desc = "Sparse Memory Device";
    dc->realize = sparse_mem_realize;
}

static const TypeInfo sparse_mem_types[] = {
    {
        .name = TYPE_SPARSE_MEM,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(SparseMemState),
        .class_init = sparse_mem_class_init,
    },
};
DEFINE_TYPES(sparse_mem_types);
