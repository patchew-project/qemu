/*
 * RISC-V WorldGuard Checker Device
 *
 * Copyright (c) 2022 SiFive, Inc.
 *
 * This provides WorldGuard Checker model.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "exec/exec-all.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/misc/riscv_worldguard.h"
#include "target/riscv/cpu.h"
#include "trace.h"

/* Common */
REG32(VENDOR,               0x000)
REG32(IMPID,                0x004)

/* wgChecker */
REG32(NSLOTS,               0x008)
REG64(ERRCAUSE,             0x010)
    FIELD(ERRCAUSE, WID,        0,  8)
    FIELD(ERRCAUSE, R,          8,  1)
    FIELD(ERRCAUSE, W,          9,  1)
    FIELD(ERRCAUSE, BE,         62, 1)
    FIELD(ERRCAUSE, IP,         63, 1)

#define ERRCAUSE_MASK   \
    (R_ERRCAUSE_WID_MASK | \
     R_ERRCAUSE_R_MASK   | \
     R_ERRCAUSE_W_MASK   | \
     R_ERRCAUSE_BE_MASK  | \
     R_ERRCAUSE_IP_MASK)

REG64(ERRADDR,              0x018)
REG64(WGC_SLOT,             0x020)

/* wgChecker slots */
REG64(SLOT_ADDR,            0x000)
REG64(SLOT_PERM,            0x008)
REG32(SLOT_CFG,             0x010)
    FIELD(SLOT_CFG, A,          0,  2)
    FIELD(SLOT_CFG, ER,         8,  1)
    FIELD(SLOT_CFG, EW,         9,  1)
    FIELD(SLOT_CFG, IR,         10, 1)
    FIELD(SLOT_CFG, IW,         11, 1)
    FIELD(SLOT_CFG, LOCK,       31, 1)

#define SLOT_SIZE               0x020

#define SLOT0_CFG_MASK \
    (R_SLOT_CFG_ER_MASK | \
     R_SLOT_CFG_EW_MASK | \
     R_SLOT_CFG_IR_MASK | \
     R_SLOT_CFG_IW_MASK | \
     R_SLOT_CFG_LOCK_MASK)

#define SLOT_CFG_MASK \
    (R_SLOT_CFG_A_MASK  | (SLOT0_CFG_MASK))

#define WGC_SLOT_END(nslots) \
    (A_WGC_SLOT + SLOT_SIZE * (nslots + 1))

/* wgChecker slot is 4K alignment */
#define WG_ALIGNED_SIZE         (1 << 12)
#define WG_ALIGNED_MASK         MAKE_64BIT_MASK(0, 12)

/* wgChecker slot address is (addr / 4). */
#define TO_SLOT_ADDR(addr)      ((addr) >> 2)
#define FROM_SLOT_ADDR(addr)    ((addr) << 2)

/* wgChecker slot cfg.A[1:0] */
#define A_OFF                   0
#define A_TOR                   1
#define A_NA4                   2
#define A_NAPOT                 3

/* wgChecker slot perm */
#define WGC_PERM(wid, perm)     ((uint64_t)(perm) << (2 * (wid)))
#define P_READ                  (1 << 0)
#define P_WRITE                 (1 << 1)

/*
 * Accesses only reach these read and write functions if the wgChecker
 * is blocking them; non-blocked accesses go directly to the downstream
 * memory region without passing through this code.
 */
static MemTxResult riscv_wgc_mem_blocked_read(void *opaque, hwaddr addr,
                                               uint64_t *pdata,
                                               unsigned size, MemTxAttrs attrs)
{
    uint32_t wid = mem_attrs_to_wid(attrs);

    trace_riscv_wgc_mem_blocked_read(addr, size, wid);

    *pdata = 0;
    return MEMTX_OK;
}

static MemTxResult riscv_wgc_mem_blocked_write(void *opaque, hwaddr addr,
                                               uint64_t value,
                                               unsigned size, MemTxAttrs attrs)
{
    uint32_t wid = mem_attrs_to_wid(attrs);

    trace_riscv_wgc_mem_blocked_write(addr, value, size, wid);

    return MEMTX_OK;
}

static const MemoryRegionOps riscv_wgc_mem_blocked_ops = {
    .read_with_attrs = riscv_wgc_mem_blocked_read,
    .write_with_attrs = riscv_wgc_mem_blocked_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
};

static IOMMUTLBEntry riscv_wgc_translate(IOMMUMemoryRegion *iommu,
                                          hwaddr addr, IOMMUAccessFlags flags,
                                          int iommu_idx)
{
    WgCheckerRegion *region = container_of(iommu, WgCheckerRegion, upstream);
    RISCVWgCheckerState *s = RISCV_WGCHECKER(region->wgchecker);
    hwaddr phys_addr;
    uint64_t region_size;

    IOMMUTLBEntry ret = {
        .iova = addr & ~WG_ALIGNED_MASK,
        .translated_addr = addr & ~WG_ALIGNED_MASK,
        .addr_mask = WG_ALIGNED_MASK,
        .perm = IOMMU_RW,
    };

    /* addr shouldn't exceed region size of down/upstream. */
    region_size = memory_region_size(region->downstream);
    g_assert(addr < region_size);

    /*
     * Look at the wgChecker configuration for this address, and
     * return a TLB entry directing the transaction at either
     * downstream_as or blocked_io_as, as appropriate.
     * For the moment, always permit accesses.
     */

    /* Use physical address instead of offset */
    phys_addr = addr + region->region_offset;

    is_success = true;

    trace_riscv_wgc_translate(phys_addr, flags,
        iommu_idx, is_success ? "pass" : "block");

    ret.target_as = is_success ? &region->downstream_as : &region->blocked_io_as;
    return ret;
}

static int riscv_wgc_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    return mem_attrs_to_wid(attrs);
}

static int riscv_wgc_num_indexes(IOMMUMemoryRegion *iommu)
{
    return worldguard_config->nworlds;
}

static uint64_t riscv_wgchecker_readq(void *opaque, hwaddr addr)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(opaque);
    uint64_t val = 0;

    if ((addr >= A_WGC_SLOT) && (addr < WGC_SLOT_END(s->slot_count))) {
        /* Read from WGC slot */
        int slot_id = (addr - A_WGC_SLOT) / SLOT_SIZE;
        int slot_offset = (addr - A_WGC_SLOT) % SLOT_SIZE;

        switch (slot_offset) {
        case A_SLOT_ADDR:
            val = s->slots[slot_id].addr;
            break;
        case A_SLOT_PERM:
            val = s->slots[slot_id].perm;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 8);
            break;
        }

        return val;
    }

    switch (addr) {
    case A_ERRCAUSE:
        val = s->errcause & ERRCAUSE_MASK;
        break;
    case A_ERRADDR:
        val = s->erraddr;
        break;
    case A_NSLOTS:
        val = s->slot_count;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                      __func__, addr, 8);
        break;
    }

    return val;
}

static uint64_t riscv_wgchecker_readl(void *opaque, hwaddr addr)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(opaque);
    uint64_t val = 0;

    if ((addr >= A_WGC_SLOT) && (addr < WGC_SLOT_END(s->slot_count))) {
        /* Read from WGC slot */
        int slot_id = (addr - A_WGC_SLOT) / SLOT_SIZE;
        int slot_offset = (addr - A_WGC_SLOT) % SLOT_SIZE;

        switch (slot_offset) {
        case A_SLOT_ADDR:
            val = extract64(s->slots[slot_id].addr, 0, 32);
            break;
        case A_SLOT_ADDR + 4:
            val = extract64(s->slots[slot_id].addr, 32, 32);
            break;
        case A_SLOT_PERM:
            val = extract64(s->slots[slot_id].perm, 0, 32);
            break;
        case A_SLOT_PERM + 4:
            val = extract64(s->slots[slot_id].perm, 32, 32);
            break;
        case A_SLOT_CFG:
            val = s->slots[slot_id].cfg & SLOT_CFG_MASK;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 4);
            break;
        }

        return val;
    }

    switch (addr) {
    case A_VENDOR:
        val = 0;
        break;
    case A_IMPID:
        val = 0;
        break;
    case A_NSLOTS:
        val = extract64(s->slot_count, 0, 32);
        break;
    case A_NSLOTS + 4:
        val = extract64(s->slot_count, 0, 32);
        break;
    case A_ERRCAUSE:
        val = s->errcause & ERRCAUSE_MASK;
        val = extract64(val, 0, 32);
        break;
    case A_ERRCAUSE + 4:
        val = s->errcause & ERRCAUSE_MASK;
        val = extract64(val, 32, 32);
        break;
    case A_ERRADDR:
        val = extract64(s->erraddr, 0, 32);
        break;
    case A_ERRADDR + 4:
        val = extract64(s->erraddr, 32, 32);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                      __func__, addr, 4);
        break;
    }

    return val;
}

static uint64_t riscv_wgchecker_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (size) {
    case 8:
        val = riscv_wgchecker_readq(opaque, addr);
        break;
    case 4:
        val = riscv_wgchecker_readl(opaque, addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid read size %u to wgChecker\n",
                      __func__, size);
        return 0;
    }

    return val;
}

/*
 * Validate the WGC slot address is between address range.
 *
 * Fix the slot address to the start address if it's not within the address range.
 * We need validation when changing "slot address" or "TOR/NAPOT mode (cfg.A)"
 */
static void validate_slot_address(void *opaque, int slot_id)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(opaque);
    uint64_t start = TO_SLOT_ADDR(s->addr_range_start);
    uint64_t end = TO_SLOT_ADDR(s->addr_range_start + s->addr_range_size);
    uint32_t cfg_a = FIELD_EX32(s->slots[slot_id].cfg, SLOT_CFG, A);

    /* First and last slot address are hard-coded. */
    if ((slot_id == 0) || (slot_id == s->slot_count)) {
        return;
    }

    /* Check WGC slot address is between address range. */
    if ((s->slots[slot_id].addr < start) || (s->slots[slot_id].addr >= end)) {
        s->slots[slot_id].addr = start;
    }

    /* Check WGC slot is 4k-aligned. */
    if (cfg_a == A_TOR) {
        s->slots[slot_id].addr &= ~TO_SLOT_ADDR(WG_ALIGNED_MASK);
    } else if (cfg_a == A_NAPOT) {
        s->slots[slot_id].addr |= TO_SLOT_ADDR(WG_ALIGNED_MASK >> 1);
    } else if (cfg_a == A_NA4) {
        /* Forcely replace NA4 slot with 4K-aligned NAPOT slot. */
        FIELD_DP32(s->slots[slot_id].cfg, SLOT_CFG, A, A_NAPOT);
        s->slots[slot_id].addr |= TO_SLOT_ADDR(WG_ALIGNED_MASK >> 1);
    }
}

static bool slots_reg_is_ro(int slot_id, int slot_offset, uint32_t nslots)
{
    /*
     * Special slots:
     *   - slot[0]:
     *     - addr is RO
     *     - perm is RO
     *     - cfg.A is OFF
     *
     *   - slot[nslots]:
     *     - addr is RO
     *     - cfg.A is OFF or TOR
     */
    if (slot_id == 0) {
        switch (slot_offset) {
        case A_SLOT_ADDR:
        case A_SLOT_ADDR + 4:
        case A_SLOT_PERM:
        case A_SLOT_PERM + 4:
            return true;
        default:
            break;
        }
    } else if (slot_id == nslots) {
        switch (slot_offset) {
        case A_SLOT_ADDR:
        case A_SLOT_ADDR + 4:
            return true;
        default:
            break;
        }
    }

    return false;
}

static void riscv_wgchecker_writeq(void *opaque, hwaddr addr,
                                    uint64_t value)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(opaque);

    if ((addr >= A_WGC_SLOT) && (addr < WGC_SLOT_END(s->slot_count))) {
        /* Read from WGC slot */
        int slot_id = (addr - A_WGC_SLOT) / SLOT_SIZE;
        int slot_offset = (addr - A_WGC_SLOT) % SLOT_SIZE;
        bool locked = FIELD_EX32(s->slots[slot_id].cfg, SLOT_CFG, LOCK);

        if (locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Couldn't write access to locked wgChecker Slot: "
                          "slot = %d, offset = %d\n", __func__, slot_id,
                          slot_offset);
            return;
        }

        if (slots_reg_is_ro(slot_id, slot_offset, s->slot_count)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 8);
        }

        switch (slot_offset) {
        case A_SLOT_ADDR:
            s->slots[slot_id].addr = value;
            validate_slot_address(s, slot_id);
            break;
        case A_SLOT_PERM:
            value &= wgc_slot_perm_mask;
            s->slots[slot_id].perm = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 8);
            break;
        }

        return;
    }

    switch (addr) {
    case A_ERRCAUSE:
        s->errcause = value & ERRCAUSE_MASK;
        break;
    case A_ERRADDR:
        s->erraddr = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                      __func__, addr, 8);
        break;
    }
}

static void riscv_wgchecker_writel(void *opaque, hwaddr addr,
                                    uint64_t value)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(opaque);

    if ((addr >= A_WGC_SLOT) && (addr < WGC_SLOT_END(s->slot_count))) {
        /* Write to WGC slot */
        int slot_id = (addr - A_WGC_SLOT) / SLOT_SIZE;
        int slot_offset = (addr - A_WGC_SLOT) % SLOT_SIZE;
        bool locked = FIELD_EX32(s->slots[slot_id].cfg, SLOT_CFG, LOCK);
        int cfg_a, old_cfg_a;

        if (locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Couldn't write access to locked wgChecker Slot: "
                          "slot = %d, offset = %d\n", __func__, slot_id,
                          slot_offset);
            return;
        }

        if (slots_reg_is_ro(slot_id, slot_offset, s->slot_count)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 4);
        }

        switch (slot_offset) {
        case A_SLOT_ADDR:
            s->slots[slot_id].addr = deposit64(
                s->slots[slot_id].addr, 0, 32, value);
            validate_slot_address(s, slot_id);
            break;
        case A_SLOT_ADDR + 4:
            s->slots[slot_id].addr = deposit64(
                s->slots[slot_id].addr, 32, 32, value);
            validate_slot_address(s, slot_id);
            break;
        case A_SLOT_PERM:
            value &= wgc_slot_perm_mask;
            s->slots[slot_id].perm = deposit64(
                s->slots[slot_id].perm, 0, 32, value);
            break;
        case A_SLOT_PERM + 4:
            value &= extract64(wgc_slot_perm_mask, 32, 32);
            s->slots[slot_id].perm = deposit64(
                s->slots[slot_id].perm, 32, 32, value);
            break;
        case A_SLOT_CFG:
            if (slot_id == 0) {
                value &= SLOT0_CFG_MASK;
                s->slots[0].cfg = value;
            } else if (slot_id == s->slot_count) {
                old_cfg_a = FIELD_EX32(s->slots[s->slot_count].cfg, SLOT_CFG, A);
                cfg_a = FIELD_EX32(value, SLOT_CFG, A);

                value &= SLOT0_CFG_MASK;
                if ((cfg_a == A_OFF) || (cfg_a == A_TOR)) {
                    value |= cfg_a;
                } else {
                    /* slot[nslots] could only use OFF or TOR config. */
                    value |= old_cfg_a;
                }
                s->slots[s->slot_count].cfg = value;

                validate_slot_address(s, slot_id);
            } else {
                value &= SLOT_CFG_MASK;
                s->slots[slot_id].cfg = value;
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                          __func__, addr, 4);
            break;
        }

        return;
    }

    switch (addr) {
    case A_ERRCAUSE:
        value &= extract64(ERRCAUSE_MASK, 0, 32);
        s->errcause = deposit64(s->errcause, 0, 32, value);
        break;
    case A_ERRCAUSE + 4:
        value &= extract64(ERRCAUSE_MASK, 32, 32);
        s->errcause = deposit64(s->errcause, 32, 32, value);
        break;
    case A_ERRADDR:
        s->erraddr = deposit64(s->erraddr, 0, 32, value);
        break;
    case A_ERRADDR + 4:
        s->erraddr = deposit64(s->erraddr, 32, 32, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unexpected memory access to (0x%" HWADDR_PRIX ", %u) \n",
                      __func__, addr, 4);
        break;
    }
}

static void riscv_wgchecker_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    switch (size) {
    case 8:
        riscv_wgchecker_writeq(opaque, addr, value);
        break;
    case 4:
        riscv_wgchecker_writel(opaque, addr, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid write size %u to wgChecker\n",
                      __func__, size);
        break;
    }
}

static MemTxResult riscv_wgchecker_read_with_attrs(
    void *opaque, hwaddr addr, uint64_t *pdata, unsigned size,
    MemTxAttrs attrs)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(opaque);

    trace_riscv_wgchecker_mmio_read(dev->mmio[0].addr, addr, size);

    *pdata = 0;
    if (could_access_wgblocks(attrs, "wgChecker")) {
        *pdata = riscv_wgchecker_read(opaque, addr, size);
    }

    return MEMTX_OK;
}

static MemTxResult riscv_wgchecker_write_with_attrs(
    void *opaque, hwaddr addr, uint64_t data, unsigned size,
    MemTxAttrs attrs)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(opaque);

    trace_riscv_wgchecker_mmio_write(dev->mmio[0].addr, addr, size, data);

    if (could_access_wgblocks(attrs, "wgChecker")) {
        riscv_wgchecker_write(opaque, addr, data, size);
    }

    return MEMTX_OK;
}

static const MemoryRegionOps riscv_wgchecker_ops = {
    .read_with_attrs = riscv_wgchecker_read_with_attrs,
    .write_with_attrs = riscv_wgchecker_write_with_attrs,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8
    }
};

static void riscv_wgc_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = riscv_wgc_translate;
    imrc->attrs_to_index = riscv_wgc_attrs_to_index;
    imrc->num_indexes = riscv_wgc_num_indexes;
}

static const TypeInfo riscv_wgc_iommu_memory_region_info = {
    .name = TYPE_RISCV_WGC_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = riscv_wgc_iommu_memory_region_class_init,
};


#define DEFINE_REGION(N)                                                \
    DEFINE_PROP_LINK("downstream-mr[" #N "]", RISCVWgCheckerState,     \
                     mem_regions[N].downstream,                         \
                     TYPE_MEMORY_REGION, MemoryRegion *),               \
    DEFINE_PROP_UINT64("region-offset[" #N "]", RISCVWgCheckerState,   \
                       mem_regions[N].region_offset, 0)                 \

static Property riscv_wgchecker_properties[] = {
    DEFINE_PROP_UINT32("slot-count", RISCVWgCheckerState, slot_count, 0x1),
    DEFINE_PROP_UINT32("mmio-size", RISCVWgCheckerState, mmio_size, 0x1000),

    /* Assume 1 wgChecker has 16 regions at maximum (WGC_NUM_REGIONS). */
    DEFINE_REGION(0), DEFINE_REGION(1), DEFINE_REGION(2), DEFINE_REGION(3),
    DEFINE_REGION(4), DEFINE_REGION(5), DEFINE_REGION(6), DEFINE_REGION(7),
    DEFINE_REGION(8), DEFINE_REGION(9), DEFINE_REGION(10), DEFINE_REGION(11),
    DEFINE_REGION(12), DEFINE_REGION(13), DEFINE_REGION(14), DEFINE_REGION(15),

    DEFINE_PROP_UINT64("addr-range-start", RISCVWgCheckerState, addr_range_start, 0),
    DEFINE_PROP_UINT64("addr-range-size", RISCVWgCheckerState, addr_range_size, UINT64_MAX),

    /*
     * We could only set individual wgChecker to hw-bypass mode. It is
     * usually used in wgChecker of BootROM, since SW has no way to enable
     * the permission of it.
     */
    DEFINE_PROP_BOOL("hw-bypass", RISCVWgCheckerState, hw_bypass, false),
    DEFINE_PROP_END_OF_LIST(),
};

static int int_log2_down(int n)
{
    int i = 0;

    n >>= 1;

    while (n) {
        i++;
        n >>= 1;
    }

    return i;
}

static int int_log2_up(int n)
{
    return int_log2_down(n - 1) + 1;
}

/*
 * Change the address range to be NAPOT alignment.
 *
 * New address range should totally cover the origin range, but new range
 * should be configured by 1 NAPOT region (slot).
 */
static void address_range_align_napot(RISCVWgCheckerState *s)
{
    uint64_t start, end, size, new_size;

    start = s->addr_range_start;
    end = s->addr_range_start + s->addr_range_size;
    size = s->addr_range_size;

    if (size == UINT64_MAX) {
        /* Full address range. No need of NAPOT alignment. */
        return;
    }

    /* Size is the next power-of-2 number. */
    size = 1 << (int_log2_up(size));
    start = QEMU_ALIGN_DOWN(start, size);
    end = QEMU_ALIGN_UP(end, size);
    new_size = end - start;

    /*
     * If base is not aligned to region size (new_size),
     * double the region size and try it again.
     */
    while ((new_size != size) && (size != 1ULL << 63)) {
        size *= 2;
        start = QEMU_ALIGN_DOWN(start, size);
        end = QEMU_ALIGN_UP(end, size);
        new_size = end - start;
    }

    s->addr_range_start = start;
    s->addr_range_size = size;
}

static void riscv_wgchecker_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    RISCVWgCheckerState *s = RISCV_WGCHECKER(dev);
    uint64_t size;

    if (worldguard_config == NULL) {
        error_setg(errp, "Couldn't find global WorldGuard configs. "
                   "Please realize %s device at first.",
                   TYPE_RISCV_WORLDGUARD);
        return;
    }

    if (s->slot_count == 0) {
        error_setg(errp, "wgChecker slot-count couldn't be zero.");
        return;
    }

    s->slots = g_new0(WgCheckerSlot, s->slot_count + 1);

    memory_region_init_io(&s->mmio, OBJECT(dev), &riscv_wgchecker_ops, s,
                          TYPE_RISCV_WGCHECKER, s->mmio_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    /* Address range should be NAPOT alignment */
    address_range_align_napot(s);

    for (int i=0; i<WGC_NUM_REGIONS; i++) {
        WgCheckerRegion *region = &s->mem_regions[i];

        if (!region->downstream) {
            continue;
        }
        region->wgchecker = s;

        const char *upstream_name = g_strdup_printf(
            "wgchecker-upstream-%"HWADDR_PRIx, region->region_offset);
        const char *downstream_name = g_strdup_printf(
            "wgchecker-downstream-%"HWADDR_PRIx, region->region_offset);

        size = memory_region_size(region->downstream);
        memory_region_init_iommu(&region->upstream, sizeof(region->upstream),
                                 TYPE_RISCV_WGC_IOMMU_MEMORY_REGION,
                                 obj, upstream_name, size);

        /* upstream MRs are 2nd ~ (n+1)th MemoryRegion. */
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), MEMORY_REGION(&region->upstream));

        /*
         * This memory region is not exposed to users of this device as a
         * sysbus MMIO region, but is instead used internally as something
         * that our IOMMU translate function might direct accesses to.
         */
        memory_region_init_io(&region->blocked_io, obj, &riscv_wgc_mem_blocked_ops,
                              region, "wgchecker-blocked-io", size);

        address_space_init(&region->downstream_as, region->downstream,
                           downstream_name);
        address_space_init(&region->blocked_io_as, &region->blocked_io,
                           "wgchecker-blocked-io");
    }
}

static void riscv_wgchecker_unrealize(DeviceState *dev)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(dev);

    g_free(s->slots);
    if (s->num_default_slots && s->default_slots) {
        g_free(s->default_slots);
    }
}

static void riscv_wgchecker_reset_enter(Object *obj, ResetType type)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(obj);
    uint64_t start = s->addr_range_start;
    uint64_t end = s->addr_range_start + s->addr_range_size;
    int nslots = s->slot_count;

    s->errcause = 0;
    s->erraddr = 0;

    for (int i = 0; i < nslots; i++) {
        s->slots[i].addr = TO_SLOT_ADDR(start);
        s->slots[i].perm = 0;
        s->slots[i].cfg = 0;
    }
    s->slots[nslots].addr = TO_SLOT_ADDR(end);
    s->slots[nslots].perm = 0;
    s->slots[nslots].cfg = 0;

    if (s->num_default_slots != 0) {
        /*
         * Use default slots:
         *   slot[0] is hard-coded to start address, so the default slots
         *   start from slot[1].
         */
        memcpy(&s->slots[1], s->default_slots,
               sizeof(WgCheckerSlot) * s->num_default_slots);
    } else if ((s->hw_bypass) ||
               ((worldguard_config != NULL) && worldguard_config->hw_bypass)) {
        /* HW bypass mode */
        uint32_t trustedwid = worldguard_config->trustedwid;

        if (trustedwid == NO_TRUSTEDWID) {
            trustedwid = worldguard_config->nworlds - 1;
        }

        s->slots[nslots].perm = WGC_PERM(trustedwid, P_READ | P_WRITE);
        s->slots[nslots].perm &= wgc_slot_perm_mask;
        s->slots[nslots].cfg = A_TOR;
    }
}

static void riscv_wgchecker_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, riscv_wgchecker_properties);
    dc->user_creatable = true;
    dc->realize = riscv_wgchecker_realize;
    dc->unrealize = riscv_wgchecker_unrealize;
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.enter = riscv_wgchecker_reset_enter;
}

static void riscv_wgchecker_instance_init(Object *obj)
{
    RISCVWgCheckerState *s = RISCV_WGCHECKER(obj);

    s->num_default_slots = 0;
}

static const TypeInfo riscv_wgchecker_info = {
    .name          = TYPE_RISCV_WGCHECKER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = riscv_wgchecker_instance_init,
    .instance_size = sizeof(RISCVWgCheckerState),
    .class_init    = riscv_wgchecker_class_init,
};

static void riscv_wgchecker_register_types(void)
{
    type_register_static(&riscv_wgchecker_info);
    type_register_static(&riscv_wgc_iommu_memory_region_info);
}

type_init(riscv_wgchecker_register_types)

/*
 * Create WgChecker device
 */
DeviceState *riscv_wgchecker_create(hwaddr addr, uint32_t size,
                                    qemu_irq irq, uint32_t slot_count,
                                    uint64_t addr_range_start,
                                    uint64_t addr_range_size,
                                    uint32_t num_of_region,
                                    MemoryRegion **downstream,
                                    uint64_t *region_offset,
                                    uint32_t num_default_slots,
                                    WgCheckerSlot *default_slots)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_WGCHECKER);
    RISCVWgCheckerState *s = RISCV_WGCHECKER(dev);
    char name_mr[32];
    char name_offset[32];
    int i;

    qdev_prop_set_uint32(dev, "slot-count", slot_count);
    qdev_prop_set_uint32(dev, "mmio-size", size);
    qdev_prop_set_uint64(dev, "addr-range-start", addr_range_start);
    if (addr_range_size) {
        qdev_prop_set_uint64(dev, "addr-range-size", addr_range_size);
    }

    g_assert(num_of_region <= WGC_NUM_REGIONS);
    for (i=0; i<num_of_region; i++) {
        snprintf(name_mr, 32, "downstream-mr[%d]", i);
        snprintf(name_offset, 32, "region-offset[%d]", i);

        object_property_set_link(OBJECT(dev), name_mr,
                                 OBJECT(downstream[i]), &error_fatal);
        qdev_prop_set_uint64(dev, name_offset, region_offset[i]);
    }

    if (num_default_slots > slot_count) {
        num_default_slots = slot_count;
    }

    s->num_default_slots = num_default_slots;
    if (s->num_default_slots) {
        s->default_slots = g_new0(WgCheckerSlot, s->num_default_slots);
        memcpy(s->default_slots, default_slots,
               sizeof(WgCheckerSlot) * s->num_default_slots);
    } else {
        s->default_slots = NULL;
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}
