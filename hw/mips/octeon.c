/*
 * QEMU Cavium Octeon board model.
 *
 * Copyright (c) 2026 Kirill A. Korinsky
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/sysbus.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "system/blockdev.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/memory.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "exec/cpu-interrupt.h"
#include "qemu/error-report.h"
#include "qemu/atomic.h"
#include "hw/mips/octeon_internal.h"
#include "target/mips/cpu.h"

#define TYPE_OCTEON_MACHINE MACHINE_TYPE_NAME("octeon3")
OBJECT_DECLARE_SIMPLE_TYPE(OcteonMachineState, OCTEON_MACHINE)

#define TYPE_OCTEON_PERIPHERAL "octeon-peripheral"
OBJECT_DECLARE_TYPE(OcteonPeripheralState, OcteonPeripheralClass,
                    OCTEON_PERIPHERAL)

#define TYPE_OCTEON_RST "octeon-rst"
OBJECT_DECLARE_SIMPLE_TYPE(OcteonRstState, OCTEON_RST)
#define TYPE_OCTEON_INTC "octeon-intc"
OBJECT_DECLARE_SIMPLE_TYPE(OcteonIntcState, OCTEON_INTC)
#define TYPE_OCTEON_CSR_BANK "octeon-csr-bank"
OBJECT_DECLARE_SIMPLE_TYPE(OcteonCsrBankState, OCTEON_CSR_BANK)

#define OCTEON_CSR_64BIT_SIZE       8

/*
 * Clock defaults follow a Ubiquiti E1000 EBB7304 U-Boot banner.
 * DDR follows the upstream EBB7304 U-Boot board default.
 */
#define OCTEON_DEFAULT_CPU_HZ       1800000000
#define OCTEON_DEFAULT_REF_HZ       50000000
#define OCTEON_DEFAULT_IO_HZ        1000000000
#define OCTEON_DEFAULT_DDR_HZ       800000000

/*
 * The physical layout follows U-Boot's generic Octeon III
 * OCTEON_MAX_PHY_MEM_SIZE value. Keep 1 GiB as the tested default.
 */
#define OCTEON_DEFAULT_RAM_SIZE     (1 * GiB)
#define OCTEON_MAX_PHY_MEM_SIZE     (512 * GiB)

/*
 * The EBB7304 FDT exposes DRAM below and above the
 * 0x10000000..0x1fffffff boot-bus/EJTAG hole.
 */
#define OCTEON_DR0_BASE             0x000000000ULL
#define OCTEON_DR0_SIZE             (256 * MiB)
#define OCTEON_DR1_BASE             0x020000000ULL
#define OCTEON_DR1_SIZE             (OCTEON_MAX_PHY_MEM_SIZE - OCTEON_DR0_SIZE)

#define OCTEON_CIU_BASE             0x1070000000000ULL
#define OCTEON_CIU_LEGACY_PAGE_SIZE (4 * KiB)
#define OCTEON_CIU_SIZE             (1 * MiB + OCTEON_CIU_LEGACY_PAGE_SIZE)
#define OCTEON_CIU_FUSE             0x728
#define OCTEON_CIU_SUM_IPI          0x0000000100000000ULL
#define OCTEON_CIU_SUM_UART         0x0000000800000000ULL
#define OCTEON_CIU_ENABLE0          0x88006f1800000000ULL
#define OCTEON_CIU_IPI_SUM_BASE     0x008
#define OCTEON_CIU_IPI_SUM_STRIDE   0x10
#define OCTEON_CIU_IPI_EN_BASE      0x210
#define OCTEON_CIU_IPI_EN_STRIDE    0x20
#define OCTEON_CIU_MBOX_SET_BASE    0x600
#define OCTEON_CIU_MBOX_CLR_BASE    0x680
#define OCTEON_CIU_MBOX_SET_STRIDE  8
#define OCTEON_CIU_GPIO_RX_DAT      0x880
#define OCTEON_CIU_GPIO_TX_SET      0x888
#define OCTEON_CIU_GPIO_TX_CLR      0x890
#define OCTEON_CIU_GPIO_BIT_CFG_STRIDE OCTEON_CSR_64BIT_SIZE
#define OCTEON_CIU_GPIO_BIT_CFGX(x) \
    (0x900 + (x) * OCTEON_CIU_GPIO_BIT_CFG_STRIDE)
#define OCTEON_CIU_GPIO_INPUTS      ((1ULL << OCTEON_CIU_GPIO_COUNT) - 1)
#define OCTEON_CIU_GPIO_BIT_CFG_TX_OE (1ULL << 0)

#define OCTEON_CIU3_BASE            0x1010000000000ULL
#define OCTEON_CIU3_SIZE            0xb0000000ULL
#define OCTEON_CIU3_PP_RST          0x100
#define OCTEON_CIU3_PP_RST_PENDING  0x110
#define OCTEON_CIU3_NMI             0x160
#define OCTEON_CIU3_FUSE            0x1a0
#define OCTEON_CIU3_IDT_CTL(idt)    ((idt) * 8 + 0x110000U)
#define OCTEON_CIU3_IDT_PP(idt)     ((idt) * 32 + 0x120000U)
#define OCTEON_CIU3_IDT_IO(idt)     ((idt) * 8 + 0x130000U)
#define OCTEON_CIU3_DEST_PP_INT(cpu) ((cpu) * 8 + 0x200000U)
#define OCTEON_CIU3_DEST_PP_INT_INTSN_SHIFT 32
#define OCTEON_CIU3_DEST_PP_INT_INTR 0x0000000000000001ULL
#define OCTEON_CIU3_IDT_CTL_IP_NUM  0x7
#define OCTEON_CIU3_CP0_IRQ_BASE    2
#define OCTEON_CIU3_ISC_CTL_BASE    0x80000000U
#define OCTEON_CIU3_ISC_W1C_BASE    0x90000000U
#define OCTEON_CIU3_ISC_W1S_BASE    0xa0000000U
#define OCTEON_CIU3_ISC_CTL_IDT     0x0000000000ff0000ULL
#define OCTEON_CIU3_ISC_CTL_IDT_SHIFT 16
#define OCTEON_CIU3_ISC_CTL_IMP     0x0000000000008000ULL
#define OCTEON_CIU3_ISC_CTL_EN      0x0000000000000002ULL
#define OCTEON_CIU3_ISC_CTL_RAW     0x0000000000000001ULL
#define OCTEON_CIU3_SRC_LEVEL       0x00000001U
#define OCTEON_CIU3_SRC_RAW         0x00000002U
#define OCTEON_CIU3_NINTSN          (1U << 20)
#define OCTEON_CIU3_ISC_SIZE        (OCTEON_CIU3_NINTSN * 8)
#define OCTEON_CIU3_MBOX_INTSN(cpu) ((cpu) + 0x4000U)
#define OCTEON_CIU3_UART0_INTSN     0x8000
#define OCTEON_CSR_BASE             0x1180000000000ULL
#define OCTEON_CSR_SIZE             0x100000000ULL
#define OCTEON_RST_BOOT             0x6001600
#define OCTEON_RST_SOFT_RST         0x6001680
/* U-Boot uses the reset controller to park and release secondary cores. */
#define OCTEON_RST_PP_POWER         0x6001700
#define OCTEON_RST_BOOT_C_MUL_SHIFT 30
#define OCTEON_RST_BOOT_PNR_MUL_SHIFT 24
/*
 * QEMU-only backing for per-core CVMSEG. Keep it outside guest DRAM;
 * the guest sees the virtual CVMSEG window, not this physical address.
 */
#define OCTEON_CVMSEG_BASE          0x10000000000ULL
#define OCTEON_CVMSEG_SIZE          0x4000
#define OCTEON_CVMSEG_TOTAL_SIZE    (OCTEON_MAX_CPUS * OCTEON_CVMSEG_SIZE)

#define OCTEON_FLASH_BASE           0x1f400000ULL
#define OCTEON_FLASH_RESET_BASE     0x1fc00000ULL
#define OCTEON_FLASH_RESET_SIZE     (4 * MiB)
#define OCTEON_FLASH_MAIN_SECTORS   127
#define OCTEON_FLASH_MAIN_SECTOR_SIZE (64 * KiB)
#define OCTEON_FLASH_ENV_SECTORS    8
#define OCTEON_FLASH_ENV_SECTOR_SIZE 0x2000
#define OCTEON_FLASH_SIZE           \
    (OCTEON_FLASH_MAIN_SECTORS * OCTEON_FLASH_MAIN_SECTOR_SIZE + \
     OCTEON_FLASH_ENV_SECTORS * OCTEON_FLASH_ENV_SECTOR_SIZE)
#define OCTEON_FLASH_ENV_OFFSET     \
    (OCTEON_FLASH_SIZE - OCTEON_FLASH_ENV_SECTOR_SIZE)
#define OCTEON_FLASH_WIDTH          2

struct OcteonMachineState {
    MachineState parent_obj;
    OcteonState *board;
    uint64_t cpu_hz;
    uint64_t ref_hz;
    uint64_t io_hz;
    uint64_t ddr_hz;
};

struct OcteonPeripheralState {
    SysBusDevice parent_obj;
    OcteonState *board;
    unsigned int index;
};

struct OcteonPeripheralClass {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
};

struct OcteonRstState {
    OcteonPeripheralState parent_obj;
    GHashTable *regs;
    uint64_t rst_pp_power;
};

struct OcteonIntcState {
    OcteonPeripheralState parent_obj;
    MemoryRegion ciu;
    MemoryRegion ciu3;
    uint32_t ciu_mbox[OCTEON_MAX_CPUS];
    uint64_t ciu_ipi_en[OCTEON_MAX_CPUS];
    uint64_t ciu_gpio_rx;
    uint64_t ciu_gpio_tx;
    uint64_t ciu_gpio_bit_cfg[OCTEON_CIU_GPIO_COUNT];
    bool uart_pending;
    uint64_t ciu3_pp_rst;
    uint32_t ciu3_src_state[OCTEON_CIU3_SRC_COUNT];
    uint64_t ciu3_src_ctl[OCTEON_CIU3_SRC_COUNT];
    uint64_t ciu3_idt_ctl[OCTEON_CIU3_IDT_COUNT];
    uint64_t ciu3_idt_pp[OCTEON_CIU3_IDT_COUNT];
    uint64_t ciu3_idt_io[OCTEON_CIU3_IDT_COUNT];
    uint16_t ciu3_src_cursor[OCTEON_MAX_CPUS][OCTEON_CIU3_CP0_IRQ_COUNT];
    uint8_t ciu3_irq_line[OCTEON_MAX_CPUS][OCTEON_CIU3_CP0_IRQ_COUNT];
};

struct OcteonCsrBankState {
    OcteonPeripheralState parent_obj;
    MemoryRegion csr;
    GHashTable *values;
};

guint octeon_uint64_hash(gconstpointer v)
{
    uint64_t value = *(const uint64_t *)v;

    return value ^ (value >> 32);
}

gboolean octeon_uint64_equal(gconstpointer a, gconstpointer b)
{
    return *(const uint64_t *)a == *(const uint64_t *)b;
}

uint64_t octeon_read64(uint64_t value, hwaddr addr, unsigned size)
{
    if (size == 4) {
        if (addr & 4) {
            return value & 0xffffffffU;
        }
        return value >> 32;
    }

    return value;
}

uint64_t octeon_write64(uint64_t old, hwaddr addr,
                        uint64_t value, unsigned size)
{
    if (size == 4) {
        if (addr & 4) {
            return (old & 0xffffffff00000000ULL) | (value & 0xffffffffU);
        }
        return (old & 0xffffffffULL) | ((value & 0xffffffffU) << 32);
    }

    return value;
}

static uint64_t octeon_atomic_write64(uint64_t *ptr, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      uint64_t and_mask, uint64_t or_mask,
                                      uint64_t *oldp)
{
    uint64_t old;
    uint64_t new;
    uint64_t cmp = qatomic_read(ptr);

    do {
        old = cmp;
        new = (octeon_write64(old, addr, value, size) & and_mask) | or_mask;
        cmp = qatomic_cmpxchg(ptr, old, new);
    } while (old != cmp);

    if (oldp) {
        *oldp = old;
    }

    return new;
}

static void octeon_validate_clocks(OcteonMachineState *oms)
{
    if (!oms->cpu_hz || !oms->ref_hz || !oms->io_hz || !oms->ddr_hz) {
        error_report("octeon3 clock frequencies must be non-zero");
        exit(1);
    }

    if (oms->cpu_hz % oms->ref_hz || oms->io_hz % oms->ref_hz) {
        error_report("octeon3 CPU and IO clocks must be multiples of "
                     "ref-clock-hz");
        exit(1);
    }
}

bool octeon_reg_lookup(GHashTable *regs, uint64_t reg, uint64_t *value)
{
    uint64_t *stored = g_hash_table_lookup(regs, &reg);

    if (!stored) {
        return false;
    }

    *value = *stored;
    return true;
}

void octeon_reg_store(GHashTable *regs, uint64_t reg, uint64_t value)
{
    uint64_t *key = g_new(uint64_t, 1);
    uint64_t *stored = g_new(uint64_t, 1);

    *key = reg;
    *stored = value;
    g_hash_table_replace(regs, key, stored);
}

bool octeon_csr_lookup(OcteonState *s, uint64_t reg, uint64_t *value)
{
    if (reg == OCTEON_RST_SOFT_RST) {
        return octeon_reg_lookup(s->rst->regs, reg, value);
    }
    return octeon_reg_lookup(s->csr_bank->values, reg, value);
}

static void octeon_csr_store(OcteonState *s, uint64_t reg, uint64_t value)
{
    if (reg == OCTEON_RST_SOFT_RST) {
        octeon_reg_store(s->rst->regs, reg, value);
        return;
    }
    octeon_reg_store(s->csr_bank->values, reg, value);
}

static bool octeon_ciu3_source_from_intsn(uint32_t intsn,
                                          unsigned int *source)
{
    if (intsn == OCTEON_CIU3_UART0_INTSN) {
        *source = OCTEON_CIU3_SRC_UART0;
        return true;
    }
    if (intsn >= OCTEON_CIU3_MBOX_INTSN(0) &&
        intsn < OCTEON_CIU3_MBOX_INTSN(OCTEON_MAX_CPUS)) {
        *source = OCTEON_CIU3_SRC_MBOX0 +
                  intsn - OCTEON_CIU3_MBOX_INTSN(0);
        return true;
    }

    return false;
}

static uint32_t octeon_ciu3_source_intsn(unsigned int source)
{
    switch (source) {
    case OCTEON_CIU3_SRC_UART0:
        return OCTEON_CIU3_UART0_INTSN;
    }

    if (source >= OCTEON_CIU3_SRC_MBOX0 &&
        source < OCTEON_CIU3_SRC_COUNT) {
        return OCTEON_CIU3_MBOX_INTSN(source - OCTEON_CIU3_SRC_MBOX0);
    }

    g_assert_not_reached();
}

static bool octeon_ciu3_source_is_level(unsigned int source)
{
    switch (source) {
    case OCTEON_CIU3_SRC_UART0:
        return true;
    }

    return false;
}

static bool octeon_ciu3_set_level(OcteonState *s, unsigned int source,
                                  int level)
{
    uint32_t old;
    uint32_t new;
    uint32_t cmp = qatomic_read(&s->intc->ciu3_src_state[source]);

    do {
        old = cmp;
        if (level) {
            new = old | OCTEON_CIU3_SRC_LEVEL | OCTEON_CIU3_SRC_RAW;
        } else {
            new = old & ~(OCTEON_CIU3_SRC_LEVEL | OCTEON_CIU3_SRC_RAW);
        }
        if (new == old) {
            return false;
        }
        cmp = qatomic_cmpxchg(&s->intc->ciu3_src_state[source], old, new);
    } while (old != cmp);

    return true;
}

static void octeon_ciu3_clear_raw(OcteonState *s, unsigned int source)
{
    uint32_t old;
    uint32_t new;
    uint32_t cmp = qatomic_read(&s->intc->ciu3_src_state[source]);

    do {
        old = cmp;
        if (octeon_ciu3_source_is_level(source) &&
            (old & OCTEON_CIU3_SRC_LEVEL)) {
            new = old | OCTEON_CIU3_SRC_RAW;
        } else {
            new = old & ~OCTEON_CIU3_SRC_RAW;
        }
        if (new == old) {
            return;
        }
        cmp = qatomic_cmpxchg(&s->intc->ciu3_src_state[source], old, new);
    } while (old != cmp);
}

static bool octeon_ciu3_set_mbox(OcteonState *s, unsigned int cpu)
{
    return octeon_ciu3_set_level(s, OCTEON_CIU3_SRC_MBOX0 + cpu,
                                 qatomic_read(&s->intc->ciu_mbox[cpu]) != 0);
}

static bool octeon_ciu3_isc_decode(hwaddr reg, uint32_t base,
                                   uint32_t *intsn)
{
    if (reg < base || reg >= base + OCTEON_CIU3_ISC_SIZE) {
        return false;
    }

    *intsn = (reg - base) >> 3;
    return true;
}

static uint64_t octeon_ciu3_isc_read(OcteonState *s, uint32_t intsn)
{
    unsigned int src;
    uint64_t value;

    if (!octeon_ciu3_source_from_intsn(intsn, &src)) {
        return 0;
    }

    value = qatomic_read(&s->intc->ciu3_src_ctl[src]) | OCTEON_CIU3_ISC_CTL_IMP;
    if (qatomic_read(&s->intc->ciu3_src_state[src]) & OCTEON_CIU3_SRC_RAW) {
        value |= OCTEON_CIU3_ISC_CTL_RAW;
    }
    return value;
}

static bool octeon_ciu3_source_pending(OcteonState *s, unsigned int cpu_index,
                                       unsigned int irq, unsigned int src)
{
    uint64_t ctl = qatomic_read(&s->intc->ciu3_src_ctl[src]);
    unsigned int idt;
    unsigned int output;

    if (!(qatomic_read(&s->intc->ciu3_src_state[src]) & OCTEON_CIU3_SRC_RAW) ||
        !(ctl & OCTEON_CIU3_ISC_CTL_EN)) {
        return false;
    }

    idt = (ctl & OCTEON_CIU3_ISC_CTL_IDT) >>
          OCTEON_CIU3_ISC_CTL_IDT_SHIFT;
    if (idt >= OCTEON_CIU3_IDT_COUNT) {
        return false;
    }

    output = qatomic_read(&s->intc->ciu3_idt_ctl[idt]) &
             OCTEON_CIU3_IDT_CTL_IP_NUM;
    if (output >= OCTEON_CIU3_CP0_IRQ_COUNT ||
        irq != OCTEON_CIU3_CP0_IRQ_BASE + output) {
        return false;
    }

    return qatomic_read(&s->intc->ciu3_idt_pp[idt]) & (1ULL << cpu_index);
}

static bool octeon_ciu3_pending_intsn(OcteonState *s, unsigned int cpu_index,
                                      unsigned int irq, uint32_t *intsn)
{
    unsigned int src;

    for (src = 0; src < OCTEON_CIU3_SRC_COUNT; src++) {
        if (octeon_ciu3_source_pending(s, cpu_index, irq, src)) {
            *intsn = octeon_ciu3_source_intsn(src);
            return true;
        }
    }

    return false;
}

static bool octeon_ciu3_pending_intsn_from(OcteonState *s,
                                           unsigned int cpu_index,
                                           unsigned int irq,
                                           unsigned int first_src,
                                           uint32_t *intsn,
                                           unsigned int *source)
{
    unsigned int i;
    unsigned int src;

    for (i = 0; i < OCTEON_CIU3_SRC_COUNT; i++) {
        src = (first_src + i) % OCTEON_CIU3_SRC_COUNT;
        if (octeon_ciu3_source_pending(s, cpu_index, irq, src)) {
            *intsn = octeon_ciu3_source_intsn(src);
            *source = src;
            return true;
        }
    }

    return false;
}

static bool octeon_ciu3_pending_dest_intsn(OcteonState *s,
                                           unsigned int cpu_index,
                                           uint32_t *intsn)
{
    unsigned int irq;
    unsigned int irq_index;
    unsigned int src;

    for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
         irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
         irq++) {
        irq_index = irq - OCTEON_CIU3_CP0_IRQ_BASE;
        if (octeon_ciu3_pending_intsn_from(s, cpu_index, irq,
                qatomic_read(&s->intc->ciu3_src_cursor[cpu_index][irq_index]),
                intsn, &src)) {
            qatomic_set(&s->intc->ciu3_src_cursor[cpu_index][irq_index],
                        (src + 1) % OCTEON_CIU3_SRC_COUNT);
            return true;
        }
    }

    return false;
}

static void octeon_ciu3_set_cpu_irq(OcteonState *s, unsigned int cpu_index,
                                    unsigned int irq, bool level)
{
    CPUMIPSState *env = &s->cpu[cpu_index].cpu->env;
    unsigned int irq_index = irq - OCTEON_CIU3_CP0_IRQ_BASE;
    uint8_t new_level = level;
    uint8_t old_level;

    old_level = qatomic_xchg(&s->intc->ciu3_irq_line[cpu_index][irq_index],
                             new_level);
    if (old_level == new_level) {
        return;
    }

    qemu_set_irq(env->irq[irq], level);
}

static void octeon_intc_update_cpu(OcteonState *s, unsigned int cpu_index)
{
    uint32_t intsn;
    unsigned int irq;
    bool level;

    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    if (qatomic_read(&s->intc->ciu3_pp_rst) & (1ULL << cpu_index)) {
        for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
             irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
             irq++) {
            octeon_ciu3_set_cpu_irq(s, cpu_index, irq, false);
        }
        return;
    }

    for (irq = OCTEON_CIU3_CP0_IRQ_BASE;
         irq < OCTEON_CIU3_CP0_IRQ_BASE + OCTEON_CIU3_CP0_IRQ_COUNT;
         irq++) {
        level = false;
        if (irq == 3) {
            level = qatomic_read(&s->intc->ciu_mbox[cpu_index]) &&
                    (qatomic_read(&s->intc->ciu_ipi_en[cpu_index]) &
                     OCTEON_CIU_SUM_IPI);
        }

        if (octeon_ciu3_pending_intsn(s, cpu_index, irq, &intsn)) {
            level = true;
        }

        octeon_ciu3_set_cpu_irq(s, cpu_index, irq, level);
    }
}

static void octeon_intc_request_cpu_update(OcteonState *s,
                                           unsigned int cpu_index)
{
    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    octeon_intc_update_cpu(s, cpu_index);
}

static void octeon_intc_update_cpu_mask(OcteonState *s, uint64_t cpu_mask)
{
    unsigned int cpu;

    for (cpu = 0; cpu < s->cpu_count; cpu++) {
        if (cpu_mask & (1ULL << cpu)) {
            octeon_intc_request_cpu_update(s, cpu);
        }
    }
}

static void octeon_intc_update_all_cpus(OcteonState *s)
{
    unsigned int cpu;

    for (cpu = 0; cpu < s->cpu_count; cpu++) {
        octeon_intc_request_cpu_update(s, cpu);
    }
}

static uint64_t octeon_present_cpu_mask(OcteonState *s)
{
    return (1ULL << s->cpu_count) - 1;
}

static uint64_t octeon_secondary_cpu_mask(OcteonState *s)
{
    return octeon_present_cpu_mask(s) & ~1ULL;
}

static uint64_t octeon_ciu3_ctl_cpu_mask(OcteonState *s, uint64_t ctl)
{
    unsigned int idt = (ctl & OCTEON_CIU3_ISC_CTL_IDT) >>
                       OCTEON_CIU3_ISC_CTL_IDT_SHIFT;

    if (idt >= OCTEON_CIU3_IDT_COUNT) {
        return 0;
    }

    return qatomic_read(&s->intc->ciu3_idt_pp[idt]);
}

static uint64_t octeon_ciu3_source_cpu_mask(OcteonState *s, unsigned int src)
{
    return octeon_ciu3_ctl_cpu_mask(
        s, qatomic_read(&s->intc->ciu3_src_ctl[src]));
}

static uint64_t octeon_ciu_gpio_rx_value(OcteonState *s)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < OCTEON_CIU_GPIO_COUNT; i++) {
        uint64_t mask = 1ULL << i;

        if (qatomic_read(&s->intc->ciu_gpio_bit_cfg[i]) &
            OCTEON_CIU_GPIO_BIT_CFG_TX_OE) {
            if (qatomic_read(&s->intc->ciu_gpio_tx) & mask) {
                value |= mask;
            }
        } else if (qatomic_read(&s->intc->ciu_gpio_rx) & mask) {
            value |= mask;
        }
    }

    return value;
}

static uint64_t octeon_ciu_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    uint64_t value = 0;
    hwaddr reg = addr & ~7ULL;
    unsigned int cpu;

    if (reg == 0) {
        if (qatomic_read(&s->intc->uart_pending)) {
            value |= OCTEON_CIU_SUM_UART;
        }
        octeon_intc_request_cpu_update(s, 0);
        return octeon_read64(value, addr, size);
    }

    if (reg == OCTEON_CIU_IPI_EN_BASE) {
        return octeon_read64(OCTEON_CIU_ENABLE0, addr, size);
    }

    if (reg == OCTEON_CIU_FUSE) {
        value = octeon_present_cpu_mask(s);
        return octeon_read64(value, addr, size);
    }

    if (reg == OCTEON_CIU_GPIO_RX_DAT) {
        return octeon_read64(octeon_ciu_gpio_rx_value(s), addr, size);
    }

    if (reg == OCTEON_CIU_GPIO_TX_SET ||
        reg == OCTEON_CIU_GPIO_TX_CLR) {
        return octeon_read64(qatomic_read(&s->intc->ciu_gpio_tx), addr, size);
    }

    if (reg >= OCTEON_CIU_GPIO_BIT_CFGX(0) &&
        reg < OCTEON_CIU_GPIO_BIT_CFGX(OCTEON_CIU_GPIO_COUNT)) {
        unsigned int index = (reg - OCTEON_CIU_GPIO_BIT_CFGX(0)) /
                             OCTEON_CIU_GPIO_BIT_CFG_STRIDE;

        return octeon_read64(qatomic_read(&s->intc->ciu_gpio_bit_cfg[index]),
                             addr, size);
    }

    if (reg >= OCTEON_CIU_IPI_SUM_BASE &&
        reg < OCTEON_CIU_IPI_SUM_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_SUM_STRIDE &&
        ((reg - OCTEON_CIU_IPI_SUM_BASE) &
         (OCTEON_CIU_IPI_SUM_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_SUM_BASE) / OCTEON_CIU_IPI_SUM_STRIDE;
        if (cpu < s->cpu_count && qatomic_read(&s->intc->ciu_mbox[cpu])) {
            return octeon_read64(OCTEON_CIU_SUM_IPI, addr, size);
        }
    }

    if (reg >= OCTEON_CIU_IPI_EN_BASE &&
        reg < OCTEON_CIU_IPI_EN_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_EN_STRIDE &&
        ((reg - OCTEON_CIU_IPI_EN_BASE) &
         (OCTEON_CIU_IPI_EN_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_EN_BASE) / OCTEON_CIU_IPI_EN_STRIDE;
        if (cpu < s->cpu_count) {
            return octeon_read64(qatomic_read(&s->intc->ciu_ipi_en[cpu]),
                                 addr, size);
        }
    }

    if (reg >= OCTEON_CIU_MBOX_CLR_BASE &&
        reg < OCTEON_CIU_MBOX_CLR_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_CLR_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_CLR_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            return octeon_read64(qatomic_read(&s->intc->ciu_mbox[cpu]),
                                 addr, size);
        }
    }

    return 0;
}

static void octeon_ciu_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;
    unsigned int cpu;

    if (reg >= OCTEON_CIU_IPI_EN_BASE &&
        reg < OCTEON_CIU_IPI_EN_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_IPI_EN_STRIDE &&
        ((reg - OCTEON_CIU_IPI_EN_BASE) &
         (OCTEON_CIU_IPI_EN_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_IPI_EN_BASE) / OCTEON_CIU_IPI_EN_STRIDE;
        if (cpu < s->cpu_count) {
            value = octeon_atomic_write64(&s->intc->ciu_ipi_en[cpu], addr,
                                          value, size, UINT64_MAX, 0, &old);
            if (old != value) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg >= OCTEON_CIU_MBOX_SET_BASE &&
        reg < OCTEON_CIU_MBOX_SET_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_SET_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_SET_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            old = qatomic_fetch_or(&s->intc->ciu_mbox[cpu], value & UINT32_MAX);
            if ((old | (value & UINT32_MAX)) != old &&
                octeon_ciu3_set_mbox(s, cpu)) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg >= OCTEON_CIU_MBOX_CLR_BASE &&
        reg < OCTEON_CIU_MBOX_CLR_BASE +
              OCTEON_MAX_CPUS * OCTEON_CIU_MBOX_SET_STRIDE &&
        ((reg - OCTEON_CIU_MBOX_CLR_BASE) &
         (OCTEON_CIU_MBOX_SET_STRIDE - 1)) == 0) {
        cpu = (reg - OCTEON_CIU_MBOX_CLR_BASE) / OCTEON_CIU_MBOX_SET_STRIDE;
        if (cpu < s->cpu_count) {
            old = qatomic_fetch_and(&s->intc->ciu_mbox[cpu],
                                    ~(value & UINT32_MAX));
            if ((old & (value & UINT32_MAX)) &&
                octeon_ciu3_set_mbox(s, cpu)) {
                octeon_intc_request_cpu_update(s, cpu);
            }
        }
        return;
    }

    if (reg == OCTEON_CIU_GPIO_TX_SET) {
        value = octeon_write64(0, addr, value, size) &
                OCTEON_CIU_GPIO_INPUTS;
        qatomic_or(&s->intc->ciu_gpio_tx, value);
        return;
    }

    if (reg == OCTEON_CIU_GPIO_TX_CLR) {
        value = octeon_write64(0, addr, value, size) &
                OCTEON_CIU_GPIO_INPUTS;
        qatomic_and(&s->intc->ciu_gpio_tx, ~value);
        return;
    }

    if (reg >= OCTEON_CIU_GPIO_BIT_CFGX(0) &&
        reg < OCTEON_CIU_GPIO_BIT_CFGX(OCTEON_CIU_GPIO_COUNT)) {
        unsigned int index = (reg - OCTEON_CIU_GPIO_BIT_CFGX(0)) /
                             OCTEON_CIU_GPIO_BIT_CFG_STRIDE;

        octeon_atomic_write64(&s->intc->ciu_gpio_bit_cfg[index], addr, value,
                              size, UINT64_MAX, 0, NULL);
        return;
    }
}

static void octeon_cpu_park_now(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->active_tc.CP0_TCHalt = 1;
    env->tcs[0].CP0_TCHalt = 1;
    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
}

static void octeon_cpu_park_work(CPUState *cs, run_on_cpu_data data)
{
    octeon_cpu_park_now(MIPS_CPU(cs));
}

static void octeon_cpu_start_work(CPUState *cs, run_on_cpu_data data)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    cpu->mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
    env->CP0_VPEConf0 |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    env->active_tc.CP0_TCHalt = 0;
    env->tcs[0].CP0_TCHalt = 0;
    env->active_tc.CP0_TCStatus = (1 << CP0TCSt_A);
    env->tcs[0].CP0_TCStatus = (1 << CP0TCSt_A);
    env->active_tc.PC = cpu_mips_phys_to_kseg1(NULL, OCTEON_FLASH_RESET_BASE);
    cs->halted = 0;
    cpu_interrupt(cs, CPU_INTERRUPT_WAKE | CPU_INTERRUPT_EXITTB);
}

static void octeon_start_cpu(OcteonState *s, unsigned int cpu_index)
{
    MIPSCPU *cpu;

    if (cpu_index >= s->cpu_count || !s->cpu[cpu_index].cpu) {
        return;
    }

    cpu = s->cpu[cpu_index].cpu;
    async_run_on_cpu(CPU(cpu), octeon_cpu_start_work, RUN_ON_CPU_NULL);
}

static void octeon_ciu3_start_cpu(OcteonState *s, unsigned int cpu_index)
{
    if (qatomic_read(&s->intc->ciu3_pp_rst) & (1ULL << cpu_index)) {
        return;
    }

    octeon_start_cpu(s, cpu_index);
}

static void octeon_ciu3_nmi(OcteonState *s, uint64_t mask)
{
    unsigned int cpu;

    for (cpu = 1; cpu < s->cpu_count; cpu++) {
        if (mask & (1ULL << cpu)) {
            octeon_ciu3_start_cpu(s, cpu);
        }
    }
}

static void octeon_rst_pp_power_write(OcteonState *s, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    uint64_t old = s->rst->rst_pp_power;
    uint64_t changed;
    unsigned int cpu;

    value = octeon_write64(old, addr, value, size) &
            octeon_present_cpu_mask(s);
    s->rst->rst_pp_power = value;
    changed = old ^ value;

    for (cpu = 1; cpu < s->cpu_count; cpu++) {
        uint64_t mask = 1ULL << cpu;

        if (!(changed & mask)) {
            continue;
        }
        if (value & mask) {
            async_run_on_cpu(CPU(s->cpu[cpu].cpu), octeon_cpu_park_work,
                             RUN_ON_CPU_NULL);
        } else {
            octeon_start_cpu(s, cpu);
        }
    }
}

static const MemoryRegionOps octeon_ciu_ops = {
    .read = octeon_ciu_read,
    .write = octeon_ciu_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_ciu3_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t value;
    uint32_t intsn;
    unsigned int cpu;
    unsigned int idt;

    if (reg == OCTEON_CIU3_PP_RST) {
        return octeon_read64(qatomic_read(&s->intc->ciu3_pp_rst), addr, size);
    }

    if (reg == OCTEON_CIU3_PP_RST_PENDING) {
        return 0;
    }

    if (reg == OCTEON_CIU3_NMI) {
        return 0;
    }

    if (reg == OCTEON_CIU3_FUSE) {
        value = octeon_present_cpu_mask(s);
        return octeon_read64(value, addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_CTL(0) &&
        reg < OCTEON_CIU3_IDT_CTL(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_CTL(0)) >> 3;
        return octeon_read64(qatomic_read(&s->intc->ciu3_idt_ctl[idt]),
                             addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_PP(0) &&
        reg < OCTEON_CIU3_IDT_PP(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_PP(0)) >> 5;
        return octeon_read64(qatomic_read(&s->intc->ciu3_idt_pp[idt]),
                             addr, size);
    }

    if (reg >= OCTEON_CIU3_IDT_IO(0) &&
        reg < OCTEON_CIU3_IDT_IO(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_IO(0)) >> 3;
        return octeon_read64(qatomic_read(&s->intc->ciu3_idt_io[idt]),
                             addr, size);
    }

    if (reg >= OCTEON_CIU3_DEST_PP_INT(0) &&
        reg < OCTEON_CIU3_DEST_PP_INT(OCTEON_MAX_CPUS)) {
        cpu = (reg - OCTEON_CIU3_DEST_PP_INT(0)) >> 3;
        value = 0;
        if (cpu < s->cpu_count) {
            if (!(qatomic_read(&s->intc->ciu3_pp_rst) & (1ULL << cpu)) &&
                octeon_ciu3_pending_dest_intsn(s, cpu, &intsn)) {
                value = OCTEON_CIU3_DEST_PP_INT_INTR |
                        ((uint64_t)intsn <<
                         OCTEON_CIU3_DEST_PP_INT_INTSN_SHIFT);
            }
        }
        return octeon_read64(value, addr, size);
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_CTL_BASE, &intsn)) {
        return octeon_read64(octeon_ciu3_isc_read(s, intsn), addr, size);
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1C_BASE, &intsn) ||
        octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1S_BASE, &intsn)) {
        return octeon_read64(octeon_ciu3_isc_read(s, intsn), addr, size);
    }

    return 0;
}

static void octeon_ciu3_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;
    uint32_t intsn;
    unsigned int cpu;
    unsigned int idt;
    unsigned int src;

    if (reg == OCTEON_CIU3_PP_RST) {
        value = octeon_atomic_write64(&s->intc->ciu3_pp_rst, addr, value, size,
                                      octeon_present_cpu_mask(s), 0, &old);

        for (cpu = 1; cpu < s->cpu_count; cpu++) {
            uint64_t mask = 1ULL << cpu;

            if (!((old ^ value) & mask)) {
                continue;
            }
            if (value & mask) {
                async_run_on_cpu(CPU(s->cpu[cpu].cpu), octeon_cpu_park_work,
                                 RUN_ON_CPU_NULL);
            } else {
                octeon_ciu3_start_cpu(s, cpu);
            }
        }
        octeon_intc_update_all_cpus(s);
        return;
    }

    if (reg == OCTEON_CIU3_NMI) {
        octeon_ciu3_nmi(s, octeon_write64(0, addr, value, size));
        octeon_intc_update_all_cpus(s);
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_CTL(0) &&
        reg < OCTEON_CIU3_IDT_CTL(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_CTL(0)) >> 3;
        octeon_atomic_write64(&s->intc->ciu3_idt_ctl[idt], addr, value, size,
                              UINT64_MAX, 0, NULL);
        octeon_intc_update_cpu_mask(
            s, qatomic_read(&s->intc->ciu3_idt_pp[idt]));
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_PP(0) &&
        reg < OCTEON_CIU3_IDT_PP(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_PP(0)) >> 5;
        value = octeon_atomic_write64(&s->intc->ciu3_idt_pp[idt], addr, value,
                                      size, UINT64_MAX, 0, &old);
        octeon_intc_update_cpu_mask(s, old | value);
        return;
    }

    if (reg >= OCTEON_CIU3_IDT_IO(0) &&
        reg < OCTEON_CIU3_IDT_IO(OCTEON_CIU3_IDT_COUNT)) {
        idt = (reg - OCTEON_CIU3_IDT_IO(0)) >> 3;
        octeon_atomic_write64(&s->intc->ciu3_idt_io[idt], addr, value, size,
                              UINT64_MAX, 0, NULL);
        octeon_intc_update_cpu_mask(
            s, qatomic_read(&s->intc->ciu3_idt_pp[idt]));
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_CTL_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            value = octeon_atomic_write64(&s->intc->ciu3_src_ctl[src], addr,
                                          value, size,
                                          ~OCTEON_CIU3_ISC_CTL_RAW,
                                          0, &old);
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_ctl_cpu_mask(s, value));
        }
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1C_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            old = qatomic_read(&s->intc->ciu3_src_ctl[src]);
            if (value & OCTEON_CIU3_ISC_CTL_EN) {
                qatomic_and(&s->intc->ciu3_src_ctl[src],
                            ~OCTEON_CIU3_ISC_CTL_EN);
            }
            if (value & OCTEON_CIU3_ISC_CTL_RAW) {
                octeon_ciu3_clear_raw(s, src);
            }
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_source_cpu_mask(s, src));
        }
        return;
    }

    if (octeon_ciu3_isc_decode(reg, OCTEON_CIU3_ISC_W1S_BASE, &intsn)) {
        if (octeon_ciu3_source_from_intsn(intsn, &src)) {
            old = qatomic_read(&s->intc->ciu3_src_ctl[src]);
            if (value & OCTEON_CIU3_ISC_CTL_EN) {
                qatomic_or(&s->intc->ciu3_src_ctl[src], OCTEON_CIU3_ISC_CTL_EN);
            }
            if (value & OCTEON_CIU3_ISC_CTL_RAW) {
                qatomic_or(&s->intc->ciu3_src_state[src], OCTEON_CIU3_SRC_RAW);
            }
            octeon_intc_update_cpu_mask(s,
                                        octeon_ciu3_ctl_cpu_mask(s, old) |
                                        octeon_ciu3_source_cpu_mask(s, src));
        }
    }
}

static const MemoryRegionOps octeon_ciu3_ops = {
    .read = octeon_ciu3_read,
    .write = octeon_ciu3_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint64_t octeon_csr_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t value;

    switch (reg) {
    case OCTEON_RST_BOOT:
        value = ((uint64_t)(s->cpu_hz / s->ref_hz) <<
                 OCTEON_RST_BOOT_C_MUL_SHIFT) |
                ((uint64_t)(s->io_hz / s->ref_hz) <<
                 OCTEON_RST_BOOT_PNR_MUL_SHIFT);
        return octeon_read64(value, addr, size);
    case OCTEON_RST_PP_POWER:
        return octeon_read64(s->rst->rst_pp_power, addr, size);
    default:
        break;
    }

    if (!octeon_csr_lookup(s, reg, &value)) {
        value = 0;
    }
    return octeon_read64(value, addr, size);
}

static void octeon_csr_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;

    if (reg == OCTEON_RST_SOFT_RST && value) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }

    if (reg == OCTEON_RST_PP_POWER) {
        octeon_rst_pp_power_write(s, addr, value, size);
        return;
    }

    if (!octeon_csr_lookup(s, reg, &old)) {
        old = 0;
    }
    value = octeon_write64(old, addr, value, size);
    octeon_csr_store(s, reg, value);
}

static const MemoryRegionOps octeon_csr_ops = {
    .read = octeon_csr_read,
    .write = octeon_csr_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

void octeon_irq_set(void *opaque, int irq, int level)
{
    OcteonState *s = opaque;
    unsigned int src;

    switch (irq) {
    case OCTEON_IRQ_UART:
        src = OCTEON_CIU3_SRC_UART0;
        if (!octeon_ciu3_set_level(s, src, level)) {
            return;
        }
        qatomic_set(&s->intc->uart_pending, level);
        break;
    default:
        return;
    }

    octeon_intc_update_cpu_mask(s, octeon_ciu3_source_cpu_mask(s, src));
}


static void octeon_cpu_after_reset(OcteonCPUState *cs)
{
    CPUMIPSState *env = &cs->cpu->env;

    env->CP0_CvmMemCtl = 0x100;
    env->CP0_CvmCtl = 0;

    if (!cs->boot_cpu) {
        /* Secondary cores are released by the Octeon reset controller. */
        octeon_cpu_park_now(cs->cpu);
        return;
    }

    if (cs->board->firmware_entry) {
        env->active_tc.PC = cs->board->firmware_entry;
    }
}

static void octeon_create_cpus(OcteonState *s)
{
    unsigned int i;

    s->cpuclk = clock_new(OBJECT(s->machine), "cpu-refclk");
    clock_set_hz(s->cpuclk, s->cpu_hz);
    s->cpu_count = s->machine->smp.cpus;
    qatomic_set(&s->intc->ciu3_pp_rst, octeon_secondary_cpu_mask(s));

    for (i = 0; i < s->cpu_count; i++) {
        DeviceState *dev = qdev_new(s->machine->cpu_type);

        s->cpu[i].board = s;
        s->cpu[i].boot_cpu = i == 0;
        object_property_set_bool(OBJECT(dev), "big-endian", TARGET_BIG_ENDIAN,
                                 &error_abort);
        object_property_set_bool(OBJECT(dev), "start-powered-off", i != 0,
                                 &error_abort);
        qdev_connect_clock_in(dev, "clk-in", s->cpuclk);
        qdev_realize(dev, NULL, &error_abort);
        s->cpu[i].cpu = MIPS_CPU(dev);

        cpu_mips_irq_init_cpu(s->cpu[i].cpu);
        cpu_mips_clock_init(s->cpu[i].cpu);
        qemu_register_resettable(OBJECT(dev));
    }
}

static uint64_t octeon_map_ram_alias(OcteonState *s, MemoryRegion *alias,
                                     const char *name, hwaddr base,
                                     uint64_t offset, uint64_t size)
{
    MemoryRegion *sysmem = get_system_memory();

    if (!size) {
        return offset;
    }

    memory_region_init_alias(alias, NULL, name, s->machine->ram, offset, size);
    memory_region_add_subregion(sysmem, base, alias);
    return offset + size;
}

static void octeon_map_ram(OcteonState *s)
{
    MemoryRegion *sysmem = get_system_memory();
    uint64_t offset = 0;
    uint64_t remaining = s->machine->ram_size;
    uint64_t size;

    size = MIN(remaining, (uint64_t)OCTEON_DR0_SIZE);
    offset = octeon_map_ram_alias(s, &s->dr0, "octeon.dr0",
                                  OCTEON_DR0_BASE, offset, size);
    remaining -= size;

    size = MIN(remaining, (uint64_t)OCTEON_DR1_SIZE);
    octeon_map_ram_alias(s, &s->dr1, "octeon.dr1",
                         OCTEON_DR1_BASE, offset, size);

    memory_region_init_ram(&s->cvmseg, NULL, "octeon.cvmseg",
                           OCTEON_CVMSEG_TOTAL_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, OCTEON_CVMSEG_BASE, &s->cvmseg);
}

static void octeon_load_firmware(OcteonState *s)
{
    g_autofree char *filename = NULL;
    g_autofree gchar *contents = NULL;
    g_autoptr(GError) err = NULL;
    gsize len;
    uint8_t *flash;

    if (!s->machine->firmware) {
        return;
    }
    if (!s->flash) {
        error_report("Octeon boot flash is not initialized");
        exit(1);
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->machine->firmware);
    if (!filename) {
        error_report("Could not find Octeon firmware '%s'",
                     s->machine->firmware);
        exit(1);
    }

    if (!g_file_get_contents(filename, &contents, &len, &err)) {
        error_report("Could not load Octeon firmware '%s': %s",
                     filename, err->message);
        exit(1);
    }

    if (len == 0 || len > OCTEON_FLASH_ENV_OFFSET) {
        error_report("Octeon firmware '%s' has invalid size %" G_GSIZE_FORMAT,
                     filename, len);
        exit(1);
    }

    flash = memory_region_get_ram_ptr(s->flash);
    memcpy(flash, contents, len);
    s->firmware_entry = cpu_mips_phys_to_kseg1(NULL, OCTEON_FLASH_RESET_BASE);
}

static MemTxResult octeon_boot_flash_read(void *opaque, hwaddr addr,
                                          uint64_t *data, unsigned size,
                                          MemTxAttrs attrs)
{
    OcteonState *s = opaque;
    uint8_t buf[8];
    MemTxResult result = MEMTX_OK;
    unsigned int i;

    for (i = 0; i < size; i++) {
        uint64_t value;

        result |= memory_region_dispatch_read(s->flash, addr + i, &value,
                                              MO_8, attrs);
        buf[i] = value;
    }
    *data = ldn_be_p(buf, size);
    return result;
}

static MemTxResult octeon_boot_flash_write(void *opaque, hwaddr addr,
                                           uint64_t value, unsigned size,
                                           MemTxAttrs attrs)
{
    OcteonState *s = opaque;
    unsigned access_size = MIN(size, 4);

    if (size > access_size) {
        access_size = OCTEON_FLASH_WIDTH;
    }
    return memory_region_dispatch_write(s->flash, addr, value,
                                        size_memop(access_size) | MO_BE,
                                        attrs);
}

static const MemoryRegionOps octeon_boot_flash_ops = {
    .read_with_attrs = octeon_boot_flash_read,
    .write_with_attrs = octeon_boot_flash_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void octeon_init_flash(OcteonState *s)
{
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    DeviceState *dev;
    uint8_t *flash;

    dev = qdev_new(TYPE_PFLASH_CFI02);
    if (dinfo) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
    }
    qdev_prop_set_uint32(dev, "num-blocks0", OCTEON_FLASH_MAIN_SECTORS);
    qdev_prop_set_uint32(dev, "sector-length0",
                         OCTEON_FLASH_MAIN_SECTOR_SIZE);
    qdev_prop_set_uint32(dev, "num-blocks1", OCTEON_FLASH_ENV_SECTORS);
    qdev_prop_set_uint32(dev, "sector-length1",
                         OCTEON_FLASH_ENV_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", OCTEON_FLASH_WIDTH);
    qdev_prop_set_uint8(dev, "mappings", 1);
    qdev_prop_set_uint8(dev, "big-endian", TARGET_BIG_ENDIAN);
    qdev_prop_set_uint16(dev, "id0", 0x01);
    qdev_prop_set_uint16(dev, "id1", 0x7e);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_uint16(dev, "unlock-addr0", 0x555);
    qdev_prop_set_uint16(dev, "unlock-addr1", 0x2aa);
    qdev_prop_set_string(dev, "name", "octeon.bootbus-flash");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    s->flash = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    if (!dinfo) {
        flash = memory_region_get_ram_ptr(s->flash);
        memset(flash, 0xff, OCTEON_FLASH_SIZE);
    }
    memory_region_init_io(&s->boot_flash, NULL, &octeon_boot_flash_ops, s,
                          "octeon.bootbus-flash-window", OCTEON_FLASH_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_FLASH_BASE,
                                &s->boot_flash);

    memory_region_init_alias(&s->boot_flash_alias, NULL,
                             "octeon.bootbus-flash-reset-alias",
                             &s->boot_flash, 0, OCTEON_FLASH_RESET_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_FLASH_RESET_BASE,
                                &s->boot_flash_alias);
}

static void octeon_rst_reset_hold(Object *obj, ResetType type)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_GET_CLASS(obj);
    OcteonRstState *rst = OCTEON_RST(obj);

    if (opc->parent_phases.hold) {
        opc->parent_phases.hold(obj, type);
    }

    g_hash_table_remove_all(rst->regs);
    rst->rst_pp_power = octeon_secondary_cpu_mask(rst->parent_obj.board);
}

static void octeon_intc_reset_hold(Object *obj, ResetType type)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_GET_CLASS(obj);
    OcteonIntcState *intc = OCTEON_INTC(obj);
    OcteonState *s = intc->parent_obj.board;

    if (opc->parent_phases.hold) {
        opc->parent_phases.hold(obj, type);
    }

    memset(intc->ciu_mbox, 0, sizeof(intc->ciu_mbox));
    memset(intc->ciu_ipi_en, 0, sizeof(intc->ciu_ipi_en));
    qatomic_set(&intc->ciu_gpio_rx, OCTEON_CIU_GPIO_INPUTS);
    qatomic_set(&intc->ciu_gpio_tx, 0);
    memset(intc->ciu_gpio_bit_cfg, 0, sizeof(intc->ciu_gpio_bit_cfg));
    qatomic_set(&intc->uart_pending, false);
    qatomic_set(&intc->ciu3_pp_rst, octeon_secondary_cpu_mask(s));
    memset(intc->ciu3_src_state, 0, sizeof(intc->ciu3_src_state));
    memset(intc->ciu3_src_ctl, 0, sizeof(intc->ciu3_src_ctl));
    memset(intc->ciu3_idt_ctl, 0, sizeof(intc->ciu3_idt_ctl));
    memset(intc->ciu3_idt_pp, 0, sizeof(intc->ciu3_idt_pp));
    memset(intc->ciu3_idt_io, 0, sizeof(intc->ciu3_idt_io));
    memset(intc->ciu3_src_cursor, 0, sizeof(intc->ciu3_src_cursor));
    memset(intc->ciu3_irq_line, 0, sizeof(intc->ciu3_irq_line));
    octeon_intc_update_all_cpus(s);
}

static void octeon_csr_bank_reset_hold(Object *obj, ResetType type)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_GET_CLASS(obj);
    OcteonCsrBankState *bank = OCTEON_CSR_BANK(obj);

    if (opc->parent_phases.hold) {
        opc->parent_phases.hold(obj, type);
    }

    g_hash_table_remove_all(bank->values);
}

static void octeon_peripheral_class_init(ObjectClass *klass,
                                          const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
}

static GHashTable *octeon_reg_table_new(void)
{
    return g_hash_table_new_full(octeon_uint64_hash,
                                 octeon_uint64_equal,
                                 g_free, g_free);
}

static void octeon_rst_init(Object *obj)
{
    OCTEON_RST(obj)->regs = octeon_reg_table_new();
}

static void octeon_rst_finalize(Object *obj)
{
    g_hash_table_destroy(OCTEON_RST(obj)->regs);
}

static void octeon_csr_bank_init(Object *obj)
{
    OCTEON_CSR_BANK(obj)->values = octeon_reg_table_new();
}

static void octeon_csr_bank_finalize(Object *obj)
{
    g_hash_table_destroy(OCTEON_CSR_BANK(obj)->values);
}

static void octeon_rst_class_init(ObjectClass *klass, const void *data)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, octeon_rst_reset_hold,
                                       NULL, &opc->parent_phases);
}

static void octeon_intc_class_init(ObjectClass *klass, const void *data)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, octeon_intc_reset_hold,
                                       NULL, &opc->parent_phases);
}

static void octeon_csr_bank_class_init(ObjectClass *klass, const void *data)
{
    OcteonPeripheralClass *opc = OCTEON_PERIPHERAL_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL,
                                       octeon_csr_bank_reset_hold, NULL,
                                       &opc->parent_phases);
}

static OcteonPeripheralState *octeon_new_peripheral(OcteonState *s,
                                                     const char *type,
                                                     unsigned int index)
{
    DeviceState *qdev = qdev_new(type);
    OcteonPeripheralState *dev = OCTEON_PERIPHERAL(qdev);

    dev->board = s;
    dev->index = index;
    return dev;
}

static void octeon_create_peripherals(OcteonState *s)
{
    s->rst = OCTEON_RST(octeon_new_peripheral(s, TYPE_OCTEON_RST, 0));
    s->intc = OCTEON_INTC(octeon_new_peripheral(s, TYPE_OCTEON_INTC, 0));
    s->csr_bank = OCTEON_CSR_BANK(octeon_new_peripheral(
        s, TYPE_OCTEON_CSR_BANK, 0));
}

static void octeon_realize_peripheral(OcteonPeripheralState *dev)
{
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
}

static void octeon_realize_peripherals(OcteonState *s)
{
    octeon_realize_peripheral(&s->intc->parent_obj);
    octeon_realize_peripheral(&s->rst->parent_obj);
    octeon_realize_peripheral(&s->csr_bank->parent_obj);
}

static void mips_octeon_init(MachineState *machine)
{
    OcteonMachineState *oms = OCTEON_MACHINE(machine);
    OcteonState *s = g_new0(OcteonState, 1);

    octeon_validate_clocks(oms);

    oms->board = s;
    s->machine = machine;
    s->cpu_hz = oms->cpu_hz;
    s->ref_hz = oms->ref_hz;
    s->io_hz = oms->io_hz;
    s->ddr_hz = oms->ddr_hz;
    octeon_create_peripherals(s);

    if (machine->kernel_filename) {
        error_report("-kernel is not implemented for octeon3; "
                     "boot via -bios and U-Boot");
        exit(1);
    }

    octeon_map_ram(s);
    octeon_init_flash(s);
    octeon_load_firmware(s);
    octeon_create_cpus(s);

    memory_region_init_io(&s->intc->ciu, OBJECT(s->intc), &octeon_ciu_ops, s,
                          "octeon.ciu", OCTEON_CIU_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_CIU_BASE,
                                &s->intc->ciu);

    memory_region_init_io(&s->intc->ciu3, OBJECT(s->intc),
                          &octeon_ciu3_ops, s,
                          "octeon.ciu3", OCTEON_CIU3_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_CIU3_BASE,
                                &s->intc->ciu3);

    memory_region_init_io(&s->csr_bank->csr, OBJECT(s->csr_bank),
                          &octeon_csr_ops, s,
                          "octeon.csr", OCTEON_CSR_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(), OCTEON_CSR_BASE,
                                        &s->csr_bank->csr, -1);

    octeon_realize_peripherals(s);
}

static void octeon_machine_reset(MachineState *machine, ResetType type)
{
    OcteonState *s = OCTEON_MACHINE(machine)->board;
    unsigned int i;

    qemu_devices_reset(type);
    for (i = 0; i < s->cpu_count; i++) {
        octeon_cpu_after_reset(&s->cpu[i]);
    }
}

static void octeon_machine_instance_init(Object *obj)
{
    OcteonMachineState *oms = OCTEON_MACHINE(obj);

    oms->cpu_hz = OCTEON_DEFAULT_CPU_HZ;
    oms->ref_hz = OCTEON_DEFAULT_REF_HZ;
    oms->io_hz = OCTEON_DEFAULT_IO_HZ;
    oms->ddr_hz = OCTEON_DEFAULT_DDR_HZ;

    object_property_add_uint64_ptr(obj, "cpu-clock-hz", &oms->cpu_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "cpu-clock-hz",
                                    "CPU clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ref-clock-hz", &oms->ref_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ref-clock-hz",
                                    "Reference clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "io-clock-hz", &oms->io_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "io-clock-hz",
                                    "IO clock frequency in Hz");

    object_property_add_uint64_ptr(obj, "ddr-clock-hz", &oms->ddr_hz,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "ddr-clock-hz",
                                    "DDR clock frequency in Hz");
}

static void octeon_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Cavium Octeon III / EBB7304 EVK";
    mc->init = mips_octeon_init;
    mc->reset = octeon_machine_reset;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("OcteonCN73XX");
    mc->default_ram_size = OCTEON_DEFAULT_RAM_SIZE;
    mc->default_ram_id = "octeon.ram";
    mc->max_cpus = OCTEON_MAX_CPUS;
}

static const TypeInfo octeon_machine_types[] = {
    {
        .name = TYPE_OCTEON_PERIPHERAL,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(OcteonPeripheralState),
        .class_size = sizeof(OcteonPeripheralClass),
        .class_init = octeon_peripheral_class_init,
        .abstract = true,
    },
    {
        .name = TYPE_OCTEON_RST,
        .parent = TYPE_OCTEON_PERIPHERAL,
        .instance_size = sizeof(OcteonRstState),
        .instance_init = octeon_rst_init,
        .instance_finalize = octeon_rst_finalize,
        .class_init = octeon_rst_class_init,
    },
    {
        .name = TYPE_OCTEON_INTC,
        .parent = TYPE_OCTEON_PERIPHERAL,
        .instance_size = sizeof(OcteonIntcState),
        .class_init = octeon_intc_class_init,
    },
    {
        .name = TYPE_OCTEON_CSR_BANK,
        .parent = TYPE_OCTEON_PERIPHERAL,
        .instance_size = sizeof(OcteonCsrBankState),
        .instance_init = octeon_csr_bank_init,
        .instance_finalize = octeon_csr_bank_finalize,
        .class_init = octeon_csr_bank_class_init,
    },
    {
        .name = TYPE_OCTEON_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(OcteonMachineState),
        .instance_init = octeon_machine_instance_init,
        .class_init = octeon_machine_class_init,
    }
};

DEFINE_TYPES(octeon_machine_types)
