/*
 * MMIO Fuzzing Target
 *
 * Resolve MemoryRegion Object in process, then directly
 * access it using memory_region_dispatch_read/write calls.
 *
 * Copyright 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "fuzz.h"

enum {
    MEM8WRITE8  = 0x00,
    MEM8WRITE16,
    MEM8WRITE32,
    MEM8WRITE64,

    MEM16WRITE8,
    MEM16WRITE16,
    MEM16WRITE32,
    MEM16WRITE64,

    MEM32WRITE8,
    MEM32WRITE16,
    MEM32WRITE32,
    MEM32WRITE64,

    MEM8READ8   = 0x10,
    MEM8READ16,
    MEM8READ32,
    MEM8READ64,

    MEM16READ8,
    MEM16READ16,
    MEM16READ32,
    MEM16READ64,

    MEM32READ8,
    MEM32READ16,
    MEM32READ32,
    MEM32READ64,

    OP_MASK     = 0x1f
};

/* We interpret the fuzzer input as a sequence of packets */
typedef struct {
    union {
        uint8_t opcode;
    };
    union {
        struct {
            uint8_t addr;
            union {
                uint8_t val8[8];
                uint16_t val16[4];
                uint32_t val32[2];
                uint64_t val64[1];
            };
        } QEMU_PACKED mem8;
        struct {
            uint16_t addr;
            union {
                uint8_t val8[8];
                uint16_t val16[4];
                uint32_t val32[2];
                uint64_t val64[1];
            };
        } QEMU_PACKED mem16;
        struct {
            uint32_t addr;
            union {
                uint8_t val8[8];
                uint16_t val16[4];
                uint32_t val32[2];
                uint64_t val64[1];
            };
        } QEMU_PACKED mem32;
        /* mem64 not supported */
    };
} QEMU_PACKED pkt;

static void memory_region_fuzz_one(QTestState *s,
                                   DeviceState *dev,
                                   MemoryRegion *mr,
                                   const unsigned char *Data,
                                   size_t Size,
                                   bool do_not_reset)
{
    pkt *a;
    size_t sz;
    uint64_t addr;
    uint64_t iosize;
    uint64_t iomask;
    uint64_t rdval;

    /* TODO check .valid.min/max_access_size */

    iosize = memory_region_size(mr);
    if (iosize < 0x100) {
        /* 8-bit address */
        iosize = 0x100;
    } else if (iosize < 0x10000) {
        /* 16-bit address */
        iosize = 0x10000;
    } else {
        /* 32-bit address */
        assert(is_power_of_2(iosize));
    }
    iomask = iosize - 1;

    if (!do_not_reset) {
        device_cold_reset(dev);
    }

    /* process all packets */
    while (Size != 0) {
        a = (pkt *)Data;
        switch (a->opcode & OP_MASK) {

        /* ugly but efficient macros... */
#define CASE_OP_READ(OP, OPTYPE, ADDRW, DATAW, MR) \
        case OP##ADDRW##READ##DATAW:\
            sz = sizeof(a->opcode)\
               + sizeof(a->OPTYPE##ADDRW.addr)\
               + sizeof(uint##DATAW##_t);\
            if (Size < sz) {\
                return;\
            }\
            addr = a->OPTYPE##ADDRW.addr & iomask;\
            memory_region_dispatch_read(MR, addr, &rdval,\
                                        size_memop(sizeof(uint##DATAW##_t)),\
                                        MEMTXATTRS_UNSPECIFIED);\
            break
#define CASE_OP_WRITE(OP, OPTYPE, ADDRW, DATAW, MR) \
        case OP##ADDRW##WRITE##DATAW:\
            sz = sizeof(a->opcode)\
               + sizeof(a->OPTYPE##ADDRW.addr)\
               + sizeof(uint##DATAW##_t);\
            if (Size < sz) {\
                return;\
            }\
            addr = a->OPTYPE##ADDRW.addr & iomask;\
            memory_region_dispatch_write(MR, addr,\
                                         a->OPTYPE##ADDRW.val##DATAW[0],\
                                         size_memop(sizeof(uint##DATAW##_t)),\
                                         MEMTXATTRS_UNSPECIFIED);\
            break

        /* ... now the macro make more sense? */
#define CASE_MEMOP(ADDRW, DATAW) \
        CASE_OP_READ(MEM, mem, ADDRW, DATAW, mr);\
        CASE_OP_WRITE(MEM, mem, ADDRW, DATAW, mr)
        CASE_MEMOP(8, 8);
        CASE_MEMOP(8, 16);
        CASE_MEMOP(8, 32);
        CASE_MEMOP(8, 64);
        CASE_MEMOP(16, 8);
        CASE_MEMOP(16, 16);
        CASE_MEMOP(16, 32);
        CASE_MEMOP(16, 64);
        CASE_MEMOP(32, 8);
        CASE_MEMOP(32, 16);
        CASE_MEMOP(32, 32);
        CASE_MEMOP(32, 64);
        default:
            return;
        }
        Size -= sz;
        Data += sz;
    }
    flush_events(s);
}

/* Global context, ideally instead of QTestState *s */
static struct {
    Object *dev;
    Object *mr;
    bool do_not_reset;
} g_ctx;

/* FIXME get this from command line ... */
const char *machine_name = "q35";
const char *type_name = "e1000e";
/* FIXME enumerate and select by index? */
const char *mr_name = "e1000e-mmio[0]";

static void memory_region_fuzz(QTestState *s,
                               const unsigned char *Data,
                               size_t Size)
{

    if (!g_ctx.dev || !g_ctx.mr) {
        g_ctx.dev = object_resolve_path_type("", type_name, NULL);
        assert(g_ctx.dev);
        g_ctx.mr  = object_resolve_path_component(g_ctx.dev, mr_name);
        assert(g_ctx.mr);
    }
    memory_region_fuzz_one(s, DEVICE(g_ctx.dev),
                           MEMORY_REGION(g_ctx.mr),
                           Data, Size,
                           g_ctx.do_not_reset);
}

static const char *memory_region_fuzz_argv(FuzzTarget *t)
{
    return g_strdup_printf("%s -machine %s,accel=qtest "
                           "-m 0 -display none -seed 42",
                           t->name, machine_name);
}

static const FuzzTarget memory_region_fuzz_target = {
    .name = "mr-fuzz",
    .description = "Fuzz doing I/O access to a MemoryRegion",
    .get_init_cmdline = memory_region_fuzz_argv,
    .fuzz = memory_region_fuzz
};

static void register_memory_region_fuzz_targets(void)
{
    fuzz_add_target(&memory_region_fuzz_target);
}

fuzz_target_init(register_memory_region_fuzz_targets);
