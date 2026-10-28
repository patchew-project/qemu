/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"

#include "target/riscv/cpu.h"

#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/riscv_hart.h"

#include "hw/char/dw8250.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/misc/tt_atlantis_prcm.h"
#include "hw/rtc/ds1338.h"

#include "system/system.h"
#include "system/device_tree.h"

#include "hw/riscv/tt_atlantis.h"

#include "aia.h"

#define TT_IRQCHIP_NUM_MSIS       255
#define TT_IRQCHIP_NUM_SOURCES    128
#define TT_IRQCHIP_NUM_PRIO_BITS  3
#define TT_IMSIC_GUESTS           5
#define TT_IMSIC_STRIDE           0x40000 /* Same stride for M and S */
#define TT_IMSIC_GUEST_BITS       6

/* Stride is fixed by hardware, check it's consistent with guest bits. */
QEMU_BUILD_BUG_ON(TT_IMSIC_STRIDE != (0x1000 << TT_IMSIC_GUEST_BITS));

#define TT_ACLINT_MTIME_SIZE    0x8050
#define TT_ACLINT_MTIME         0x0
#define TT_ACLINT_MTIMECMP      0x8000
#define TT_ACLINT_TIMEBASE_FREQ 1000000000

#define ASCALON_CLUSTER_ID     0
#define RCPU_CLUSTER_ID        1

static const MemMapEntry tt_atlantis_memmap[] = {
    /* Keep sorted with :'<,'>!sort -g -k 4 */
    [TT_ATL_DDR_LO] =           { 0x00000000,    0x80000000 },
    [TT_ATL_BOOTROM] =          { 0x80000000,        0x2000 },
    [TT_ATL_MIMSIC] =           { 0xa0000000,      0x200000 },
    [TT_ATL_ACLINT] =           { 0xa2180000,       0x10000 },
    [TT_ATL_SIMSIC] =           { 0xa4000000,      0x200000 },
    [TT_ATL_MAPLIC] =           { 0xcc000000,     0x4000000 },
    [TT_ATL_PRCM_RCPU] =        { 0xd0000000,       0x10000 },
    [TT_ATL_I2C0] =             { 0xd4040000,       0x10000 },
    [TT_ATL_I2C1] =             { 0xd4050000,       0x10000 },
    [TT_ATL_I2C2] =             { 0xd4060000,       0x10000 },
    [TT_ATL_I2C3] =             { 0xd4070000,       0x10000 },
    [TT_ATL_I2C4] =             { 0xd4080000,       0x10000 },
    [TT_ATL_UART0] =            { 0xd4100000,       0x10000 },
    [TT_ATL_UART1] =            { 0xd4110000,       0x10000 },
    [TT_ATL_UART2] =            { 0xd4120000,       0x10000 },
    [TT_ATL_UART3] =            { 0xd4130000,       0x10000 },
    [TT_ATL_UART4] =            { 0xd4140000,       0x10000 },
    [TT_ATL_PRCM_PCIE] =        { 0xd8000000,         0x100 },
    [TT_ATL_PRCM_MM] =          { 0xdc000000,        0x1000 },
    [TT_ATL_PRCM_HSIO] =        { 0xe00c0000,         0x510 },
    [TT_ATL_SAPLIC] =           { 0xe8000000,     0x4000000 },
    [TT_ATL_DDR_HI] =          { 0x100000000,  0x1000000000 },
};

static const MemMapEntry tt_atlantis_rcpu_memmap[] = {
    /* Keep sorted with :'<,'>!sort -g -k 4 */
    [TT_ATL_BOOTROM] =           { 0x00040000,    0x40000 },
    [TT_ATL_RCPU_SRAM] =         { 0x20100000,   0x100000 },
    [TT_ATL_PRCM_RCPU] =         { 0x40000000,    0x10000 },
    [TT_ATL_WDT0] =              { 0x40030000,    0x10000 },
    [TT_ATL_WDT1] =              { 0x40040000,    0x10000 },
    [TT_ATL_IO_CTRL] =           { 0x40060000,    0x10000 },
    [TT_ATL_RCPU_MAILBOX] =      { 0x40100000,    0x10000 },
    [TT_ATL_RCPU_SMN_FIREWALL] = { 0x41010000,    0x10000 },
    [TT_ATL_NOCM_FIREWALL] =     { 0x41020000,    0x10000 },
    [TT_ATL_UART0] =             { 0x48100000,    0x10000 },
    [TT_ATL_UART1] =             { 0x48110000,    0x10000 },
    [TT_ATL_UART2] =             { 0x48120000,    0x10000 },
    [TT_ATL_UART3] =             { 0x48130000,    0x10000 },
    [TT_ATL_UART4] =             { 0x48140000,    0x10000 },
    [TT_ATL_GPIO] =              { 0x48150000,    0x10000 },
    [TT_ATL_QSPI] =              { 0x4a000000,  0x2000000 },
    [TT_ATL_OTP] =               { 0x50000000,   0x100000 },
    [TT_ATL_PRCM_TRNG] =         { 0x50600000,    0x10000 },
    [TT_ATL_SPACC] =             { 0x50800000,   0x100000 },
    [TT_ATL_DDR_LO] =            { 0x60000000, 0x40000000 },
    [TT_ATL_MIMSIC] =            { 0xa0000000,  0x8000000 },
    [TT_ATL_PRCM_PCIE] =         { 0xa8000000,    0x10000 },
    [TT_ATL_PRCM_MM] =           { 0xb0000000,   0x100000 },
    [TT_ATL_SDHCI0] =            { 0xb4020000,    0x10000 },
    [TT_ATL_SDHCI1] =            { 0xb4030000,    0x10000 },
    [TT_ATL_PRCM_HSIO] =         { 0xb40c0000,    0x10000 },
    [TT_ATL_PRCM_DDR0] =         { 0xb8000000,    0x10000 },
    [TT_ATL_PRCM_DDR1] =         { 0xb8800000,    0x10000 },
    [TT_ATL_DDRC0_CTRL] =        { 0xb9000000,   0x800000 },
    [TT_ATL_DDRC1_CTRL] =        { 0xb9800000,   0x800000 },
    [TT_ATL_MAPLIC] =            { 0xbc000000,  0x3000000 },
    [TT_ATL_PRCM_ASC] =          { 0xbf000000,  0x1000000 },
    [TT_ATL_RCPU_CLIC] =         { 0xe000e000,     0x2000 },
    [TT_ATL_RCPU_CLINT] =        { 0xe0020000,     0xc000 },
};

static I2CBus *i2c_get_bus(TTAtlantisSoCState *s, unsigned busnr)
{
    assert(busnr < TT_ATL_NUM_I2C);

    return s->i2c[busnr].bus;
}

static uint32_t fdt_phandle = 1;
static uint32_t next_phandle(void)
{
    return fdt_phandle++;
}

static void create_fdt_memory(void *fdt, const MemMapEntry *memmap,
                              hwaddr ram_size)
{
    hwaddr size_lo = ram_size;
    hwaddr size_hi = 0;

    if (size_lo > memmap[TT_ATL_DDR_LO].size) {
        size_lo = memmap[TT_ATL_DDR_LO].size;
        size_hi = ram_size - size_lo;
    }

    riscv_create_fdt_socket_memory(fdt, memmap[TT_ATL_DDR_LO].base,
                                   size_lo, 0, false);
    if (size_hi) {
        /*
         * The first part of the HI address is aliased at the LO address
         * so do not include that as usable memory. Is there any way
         * (or good reason) to describe that aliasing 2GB with DT?
         */
        riscv_create_fdt_socket_memory(fdt,
                                       memmap[TT_ATL_DDR_HI].base + size_lo,
                                       size_hi, 0, false);
    }
}

static void create_fdt_aclint(void *fdt, RISCVHartArrayState *harts,
                              const MemMapEntry *memmap,
                              uint32_t *intc_phandles)
{
    g_autofree char *name = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    uint32_t aclint_cells_size;
    hwaddr addr;

    aclint_mtimer_cells = g_new0(uint32_t, harts->num_harts * 2);

    for (int cpu = 0; cpu < harts->num_harts; cpu++) {
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
    }
    aclint_cells_size = harts->num_harts * sizeof(uint32_t) * 2;

    addr = memmap[TT_ATL_ACLINT].base;

    name = g_strdup_printf("/soc/mtimer@%"HWADDR_PRIX, addr);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aclint-mtimer");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, addr + TT_ACLINT_MTIME,
                                 2, 0x1000,
                                 2, addr + TT_ACLINT_MTIMECMP,
                                 2, 0x1000);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aclint_mtimer_cells, aclint_cells_size);
}

static void create_fdt_one_imsic(void *fdt, const MemMapEntry *mem, int cpus,
                                 uint32_t *intc_phandles, uint32_t msi_phandle,
                                 int irq_line, uint32_t imsic_guest_bits)
{
    g_autofree char *name = NULL;
    g_autofree uint32_t *imsic_cells = g_new0(uint32_t, cpus * 2);

    for (int cpu = 0; cpu < cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    name = g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");

    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     imsic_cells, sizeof(uint32_t) * cpus * 2);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids", TT_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }
    qemu_fdt_setprop_cell(fdt, name, "phandle", msi_phandle);
}

static void create_fdt_one_aplic(void *fdt,
                                 const MemMapEntry *mem,
                                 uint32_t msi_phandle,
                                 uint32_t *intc_phandles,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle,
                                 int irq_line, int num_harts)
{
    g_autofree char *name =
        g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    g_autofree uint32_t *aplic_cells = g_new0(uint32_t, num_harts * 2);

    for (int cpu = 0; cpu < num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 0);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);

    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aplic_cells, num_harts * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", msi_phandle);

    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                          TT_IRQCHIP_NUM_SOURCES);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(fdt, name, "riscv,delegation",
                               aplic_child_phandle, 1, TT_IRQCHIP_NUM_SOURCES);
    }

    qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_phandle);
}

static void create_fdt_pmu(void *fdt, RISCVHartArrayState *harts)
{
    char pmu_name[] = "/pmu";
    RISCVCPU *hart = &harts->harts[0];

    qemu_fdt_add_subnode(fdt, pmu_name);
    qemu_fdt_setprop_string(fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(fdt, hart->pmu_avail_ctrs, pmu_name);
}

static void create_fdt_cpu(void *fdt, RISCVHartArrayState *harts,
                           const MemMapEntry *memmap,
                           uint32_t aplic_s_phandle,
                           uint32_t imsic_s_phandle)
{
    int num_harts = harts->num_harts;
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, num_harts);

    riscv_fdt_create_cpu_socket_subnode(fdt, TT_ACLINT_TIMEBASE_FREQ);

    riscv_create_fdt_socket_cpus(fdt, harts->harts, 0, num_harts,
                                 harts->hartid_base, &fdt_phandle,
                                 intc_phandles, false, false);

    create_fdt_aclint(fdt, harts, memmap, intc_phandles);

    /* M-level IMSIC node */
    uint32_t msi_m_phandle = next_phandle();
    create_fdt_one_imsic(fdt, &memmap[TT_ATL_MIMSIC], num_harts,
                         intc_phandles, msi_m_phandle,
                         IRQ_M_EXT, TT_IMSIC_GUEST_BITS);

    /* S-level IMSIC node */
    create_fdt_one_imsic(fdt, &memmap[TT_ATL_SIMSIC], num_harts,
                         intc_phandles, imsic_s_phandle,
                         IRQ_S_EXT, TT_IMSIC_GUEST_BITS);

    uint32_t aplic_m_phandle = next_phandle();

    /* M-level APLIC node */
    create_fdt_one_aplic(fdt, &memmap[TT_ATL_MAPLIC],
                         msi_m_phandle, intc_phandles,
                         aplic_m_phandle, aplic_s_phandle,
                         IRQ_M_EXT, num_harts);

    /* S-level APLIC node */
    create_fdt_one_aplic(fdt, &memmap[TT_ATL_SAPLIC],
                         imsic_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         IRQ_S_EXT, num_harts);
}

static void create_fdt_uart(void *fdt, const MemMapEntry *mem, int irq,
                            int irqchip_phandle, uint32_t clk_parent_phandle,
                            uint32_t clk_phandle)
{
    static const char * const compat[2] = {
        "snps,dw-apb-uart", "ns16550a"
    };
    g_autofree char *name = g_strdup_printf("/soc/serial@%"HWADDR_PRIX,
                                            mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string_array(fdt, name, "compatible",
                                 (char **)&compat, ARRAY_SIZE(compat));
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "reg-shift", 2);
    qemu_fdt_setprop_cell(fdt, name, "reg-io-width", 4);
    qemu_fdt_setprop_cells(fdt, name, "clocks", clk_parent_phandle,
                           clk_phandle);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", irqchip_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", irq, 0x4);

    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", name);
}

static void create_fdt_rng(void *fdt)
{
    uint8_t rng_seed[32];

    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static void create_fdt_clk(void *fdt, const char *clock_name,
                           uint32_t freq, uint32_t phandle)
{
    g_autofree char *name = g_strdup_printf("/clocks/%s", clock_name);

    qemu_fdt_add_path(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop_string(fdt, name, "clock-output-names", clock_name);
    qemu_fdt_setprop_cell(fdt, name, "#clock-cells", 0);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", freq);
    qemu_fdt_setprop_cell(fdt, name, "phandle", phandle);
}

static void create_fdt_i2c(void *fdt, const MemMapEntry *mem, uint32_t irq,
                           uint32_t irqchip_phandle,
                           uint32_t clk_parent_phandle,
                           uint32_t clk_phandle)
{
    g_autofree char *name = g_strdup_printf("/soc/i2c@%"HWADDR_PRIX, mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "snps,designware-i2c");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", irqchip_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", irq, 0x4);
    qemu_fdt_setprop_cells(fdt, name, "clocks", clk_parent_phandle,
                           clk_phandle);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 100000);
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 0);
}

static void create_fdt_i2c_device(void *fdt, const MemMapEntry *memmap,
                                  int bus, const char *compat, int addr)
{
    hwaddr base = memmap[TT_ATL_I2C0 + bus].base;
    g_autofree char *name = g_strdup_printf("/soc/i2c@%"HWADDR_PRIX"/sensor@%x",
                                            base, addr);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", compat);
    qemu_fdt_setprop_cell(fdt, name, "reg", addr);
}

static char *create_fdt_prcm(void *fdt, const MemMapEntry *mem,
                             const char *prcm_name)
{
    hwaddr base = mem->base;
    hwaddr size = mem->size;
    char *name = g_strdup_printf("/soc/clock-controller@%" PRIx64,
                                 base);
    g_autofree char *compatible =
        g_strdup_printf("tenstorrent,atlantis-prcm-%s", prcm_name);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", compatible);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, base, 2, size);
    qemu_fdt_setprop_cell(fdt, name, "#clock-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "#reset-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "phandle", next_phandle());

    return name;
}

static void finalize_fdt(void *fdt, RISCVHartArrayState *harts,
                         const MemMapEntry *memmap, hwaddr ram_size)
{
    uint32_t aplic_s_phandle = next_phandle();
    uint32_t imsic_s_phandle = next_phandle();
    uint32_t osc_24m_phandle = next_phandle();
    uint32_t prcm_rcpu_phandle;
    g_autofree char *rcpu_name, *hsio_name, *pcie_name, *mm_name;

    create_fdt_cpu(fdt, harts, memmap, aplic_s_phandle, imsic_s_phandle);

    create_fdt_memory(fdt, memmap, ram_size);

    /*
     * We want to do this, but the Linux aplic driver was broken before v6.16
     *
     * qemu_fdt_setprop_cell(MACHINE(s)->fdt, "/soc", "interrupt-parent",
     *                       aplic_s_phandle);
     */

    create_fdt_clk(fdt, "osc-24m", 24000000, osc_24m_phandle);

    rcpu_name = create_fdt_prcm(fdt, &memmap[TT_ATL_PRCM_RCPU], "rcpu");
    prcm_rcpu_phandle = qemu_fdt_get_phandle(fdt, rcpu_name);
    qemu_fdt_setprop_cells(fdt, rcpu_name, "clocks", osc_24m_phandle);
    qemu_fdt_setprop_cells(fdt, rcpu_name, "assigned-clocks",
                           prcm_rcpu_phandle, TT_ATL_CLK_RCPU_ROOT,
                           prcm_rcpu_phandle, TT_ATL_CLK_NOCC_CLK);
    qemu_fdt_setprop_cells(fdt, rcpu_name, "assigned-clock-parents",
                           prcm_rcpu_phandle, TT_ATL_CLK_RCPU_PLL,
                           prcm_rcpu_phandle, TT_ATL_CLK_NOC_PLL);

    hsio_name = create_fdt_prcm(fdt, &memmap[TT_ATL_PRCM_HSIO], "hsio");
    qemu_fdt_setprop_cells(fdt, hsio_name, "clocks", osc_24m_phandle,
                           prcm_rcpu_phandle, TT_ATL_CLK_HSIO_PLL);

    pcie_name = create_fdt_prcm(fdt, &memmap[TT_ATL_PRCM_PCIE], "pcie");
    qemu_fdt_setprop_cells(fdt, pcie_name, "clocks", osc_24m_phandle,
                           prcm_rcpu_phandle, TT_ATL_CLK_PCIE_PLL);

    mm_name = create_fdt_prcm(fdt, &memmap[TT_ATL_PRCM_MM], "mm");
    qemu_fdt_setprop_cells(fdt, mm_name, "clocks", osc_24m_phandle,
                           prcm_rcpu_phandle, TT_ATL_CLK_MM_PLL0,
                           prcm_rcpu_phandle, TT_ATL_CLK_MM_PLL1);

    create_fdt_uart(fdt, &memmap[TT_ATL_UART1], TT_ATL_UART1_IRQ,
                    aplic_s_phandle, prcm_rcpu_phandle, TT_ATL_CLK_UART1_PCLK);

    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        create_fdt_i2c(fdt,
                       &memmap[TT_ATL_I2C0 + i],
                       TT_ATL_I2C0_IRQ + i,
                       aplic_s_phandle, prcm_rcpu_phandle,
                       TT_ATL_CLK_I2C0_PCLK + i);
    }

    /* I2C peripherals: qemu specific */
    create_fdt_i2c_device(fdt, memmap, 0, "dallas,ds1338", 0x6f);
    create_fdt_i2c_device(fdt, memmap, 4, "ti,tmp105", 0x48);
}

static void create_fdt(TTAtlantisState *ams)
{
    MachineState *ms = MACHINE(ams);
    TTAscalonClusterState *ac = &ams->soc.ascalon;
    int fdt_size = 0;

    ms->fdt = riscv_create_board_device_tree("Tenstorrent Atlantis RISC-V Machine",
                                             "tenstorrent,atlantis", &fdt_size);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    create_fdt_rng(ms->fdt);

    qemu_fdt_add_subnode(ms->fdt, "/aliases");

    create_fdt_pmu(ms->fdt, &ac->cpus);
}

static void load_fdt(TTAtlantisState *ams)
{
    MachineState *ms = MACHINE(ams);
    TTAscalonClusterState *ac = &ams->soc.ascalon;
    char **node_path;
    Error *err = NULL;
    int fdt_size = 0;

    ms->fdt = load_device_tree(ms->dtb, &fdt_size);
    if (!ms->fdt) {
        error_report("load_device_tree() failed");
        exit(1);
    }

    qemu_fdt_add_path(ms->fdt, "/chosen");

    /* Clear memory nodes and update with the specified RAM size */
    node_path = qemu_fdt_node_unit_path(ms->fdt, "memory", &err);
    if (err) {
        warn_report_err(err);
    } else {
        for (int i = 0; node_path[i]; i++) {
            warn_report("Replacing device tree %s with the requested RAM size",
                        node_path[i]);
            qemu_fdt_nop_node(ms->fdt, node_path[i]);
        }
        g_strfreev(node_path);
    }

    create_fdt_memory(ms->fdt, ac->memmap, ms->ram_size);
}

static void tt_atlantis_machine_done(Notifier *n, void *data)
{
    TTAtlantisState *ams = container_of(n, TTAtlantisState, machine_done);
    TTAtlantisSoCState *s = &ams->soc;
    TTAscalonClusterState *ac = &s->ascalon;
    MachineState *machine = MACHINE(ams);
    hwaddr start_addr = ac->memmap[TT_ATL_DDR_LO].base;
    hwaddr mem_size;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&ac->cpus);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry;
    RISCVBootInfo boot_info;

    /*
     * A user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(machine->fdt, &ac->cpus, ac->memmap,
                     machine->ram_size);
    }

    mem_size = machine->ram_size;
    if (mem_size > ac->memmap[TT_ATL_DDR_LO].size) {
        mem_size = ac->memmap[TT_ATL_DDR_LO].size;
    }
    riscv_boot_info_init_discontig_mem(&boot_info, &ac->cpus,
                                       ac->memmap[TT_ATL_DDR_LO].base,
                                       mem_size);

    firmware_end_addr = riscv_find_and_load_firmware(machine, &boot_info,
                                                     firmware_name,
                                                     &start_addr, NULL);

    kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                     firmware_end_addr);
    if (machine->kernel_filename) {
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, NULL);
        kernel_entry = boot_info.image_low_addr;
    } else {
        /* If we aren't loading a payload, OpenSBI thinks we are trying to boot
         * address 0, which fails `sbi_domain_check_addr()` as that is where
         * OpenSBI is running. Instead point OpenSBI to the end of the region
         * where it was loaded, which avoids the early hang, allowing the
         * system to proceed with the OpenSBI boot output.
         */
        kernel_entry = kernel_start_addr;
    }

    fdt_load_addr = riscv_compute_fdt_addr(ac->memmap[TT_ATL_DDR_LO].base,
                                           ac->memmap[TT_ATL_DDR_LO].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &ac->cpus, start_addr,
                              ac->memmap[TT_ATL_BOOTROM].base,
                              ac->memmap[TT_ATL_BOOTROM].size,
                              kernel_entry,
                              fdt_load_addr);
}

static inline int tt_atlantis_uart_index(int uart_dev)
{
    return uart_dev - TT_ATL_UART0;
}

static void tt_rcpu_cluster_init(Object *obj)
{
    TTRCPUClusterState *rc = TT_RCPU_CLUSTER(obj);

    object_initialize_child(obj, "tt-rcpu.cluster", &rc->cluster,
                            TYPE_CPU_CLUSTER);
    object_initialize_child(OBJECT(&rc->cluster), "cpus", &rc->cpus,
                            TYPE_RISCV_HART_ARRAY);
}

static void tt_rcpu_cluster_realize(DeviceState *dev, Error **errp)
{
    TTRCPUClusterState *rc = TT_RCPU_CLUSTER(dev);

    rc->memmap = tt_atlantis_rcpu_memmap;

    /* The RCPU has its own memory view. Create it here before the CPU uses it */
    memory_region_init(&rc->memory, OBJECT(rc), "tt-rcpu.memory",
                       UINT32_MAX);

    /* The RCPU's maskrom which is inaccessible outside of the RCPU */
    if (!memory_region_init_rom(&rc->bootrom, OBJECT(rc),
                                "tt-rcpu.bootrom",
                                rc->memmap[TT_ATL_BOOTROM].size, errp)) {
        return;
    }
    memory_region_add_subregion(&rc->memory,
                                rc->memmap[TT_ATL_BOOTROM].base,
                                &rc->bootrom);

    /* RCPU SRAM */
    if (!memory_region_init_ram(&rc->sram, OBJECT(rc),
                                "tt-rcpu.sram",
                                rc->memmap[TT_ATL_RCPU_SRAM].size, errp)) {
        return;
    }
    memory_region_add_subregion(&rc->memory,
                                rc->memmap[TT_ATL_RCPU_SRAM].base,
                                &rc->sram);

    /* CPUs */
    object_property_set_str(OBJECT(&rc->cpus), "cpu-type", TYPE_RISCV_CPU_TT_RCPU,
                            &error_abort);
    object_property_set_int(OBJECT(&rc->cpus), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&rc->cpus), "num-harts", 1,
                            &error_abort);
    object_property_set_int(OBJECT(&rc->cpus), "resetvec",
                            rc->memmap[TT_ATL_BOOTROM].base,
                            &error_abort);
    object_property_set_link(OBJECT(&rc->cpus), "memory",
                             OBJECT(&rc->memory),
                             &error_abort);

    if (!sysbus_realize(SYS_BUS_DEVICE(&rc->cpus), errp)) {
        return;
    }

    qdev_prop_set_int32(DEVICE(&rc->cluster), "cluster-id", RCPU_CLUSTER_ID);
    /* cluster must be realized after the hart array */
    if (!qdev_realize(DEVICE(&rc->cluster), NULL, errp)) {
        return;
    }
}

static void tt_rcpu_cluster_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = tt_rcpu_cluster_realize;
}

static void tt_ascalon_cluster_init(Object *obj)
{
    TTAscalonClusterState *ac = TT_ASCALON_CLUSTER(obj);

    object_initialize_child(obj, "tt-ascalon.cluster", &ac->cluster,
                            TYPE_CPU_CLUSTER);
    object_initialize_child(OBJECT(&ac->cluster), "cpus", &ac->cpus,
                            TYPE_RISCV_HART_ARRAY);
}

static void tt_ascalon_cluster_realize(DeviceState *dev, Error **errp)
{
    TTAscalonClusterState *ac = TT_ASCALON_CLUSTER(dev);

    ac->memmap = tt_atlantis_memmap;

    /* Ascalon's memory view is system memory */
    memory_region_init(&ac->memory, OBJECT(ac),
                       "tt-ascalon-memory", UINT64_MAX);
    memory_region_add_subregion(get_system_memory(), 0, &ac->memory);

    /* Boot ROM */
    if (!memory_region_init_rom(&ac->bootrom, OBJECT(ac), "tt-atlantis.bootrom",
                                ac->memmap[TT_ATL_BOOTROM].size, errp)) {
        return;
    }
    memory_region_add_subregion(&ac->memory, ac->memmap[TT_ATL_BOOTROM].base,
                                &ac->bootrom);

    /* CPUs */
    object_property_set_str(OBJECT(&ac->cpus), "cpu-type", ac->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&ac->cpus), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&ac->cpus), "num-harts", ac->num_harts,
                            &error_abort);
    object_property_set_int(OBJECT(&ac->cpus), "resetvec",
                            ac->memmap[TT_ATL_BOOTROM].base,
                            &error_abort);
    object_property_set_link(OBJECT(&ac->cpus), "memory",
                             OBJECT(&ac->memory),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&ac->cpus), errp)) {
        return;
    }

    qdev_prop_set_uint32(DEVICE(&ac->cluster), "cluster-id", ASCALON_CLUSTER_ID);
    /* cluster must be realized after the hart array */
    if (!qdev_realize(DEVICE(&ac->cluster), NULL, errp)) {
        return;
    }
}

static const Property tt_ascalon_cluster_props[] = {
    DEFINE_PROP_STRING("cpu-type", TTAscalonClusterState, cpu_type),
    DEFINE_PROP_UINT32("num-harts", TTAscalonClusterState, num_harts, 8),
};

static void tt_ascalon_cluster_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = tt_ascalon_cluster_realize;
    device_class_set_props(dc, tt_ascalon_cluster_props);
}

static void tt_atlantis_soc_init(Object *obj)
{
    TTAtlantisSoCState *s = TT_ATLANTIS_SOC(obj);

    object_initialize_child(obj, "ascalon", &s->ascalon, TYPE_TT_ASCALON_CLUSTER);

    for (int i = 0; i < TT_ATL_NUM_UARTS; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i],
                                TYPE_DW8250);
    }

    object_initialize_child(obj, "prcm-rcpu",
                            &s->prcm[TT_ATLANTIS_PRCM_DOMAIN_RCPU],
                            TYPE_TT_ATLANTIS_PRCM_RCPU);
    object_initialize_child(obj, "prcm-hsio",
                            &s->prcm[TT_ATLANTIS_PRCM_DOMAIN_HSIO],
                            TYPE_TT_ATLANTIS_PRCM_HSIO);
    object_initialize_child(obj, "prcm-pcie",
                            &s->prcm[TT_ATLANTIS_PRCM_DOMAIN_PCIE],
                            TYPE_TT_ATLANTIS_PRCM_PCIE);
    object_initialize_child(obj, "prcm-mm",
                            &s->prcm[TT_ATLANTIS_PRCM_DOMAIN_MM],
                            TYPE_TT_ATLANTIS_PRCM_MM);

    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        object_initialize_child(obj, "i2c[*]", &s->i2c[i],
                                TYPE_DESIGNWARE_I2C);
    }
}

/*
 * Create and add a new alias to a memory container
 * @obj
 * @m MemoryRegion container for the alias to be added to
 * @orig MemoryRegion original region to be aliased
 *    Ascalon memory
 * @size of the subregion
 * @base address of the subregion
 * @name for the alias being added to the RCPU memory
 */
static void create_alias(Object *obj, MemoryRegion *m,
        MemoryRegion *orig, hwaddr size, hwaddr base, const char *name)
{
    MemoryRegion *alias = g_new(MemoryRegion, 1);

    memory_region_init_alias(alias, obj, name, orig, 0,
                             size);
    memory_region_add_subregion(m, base, alias);
}

/*
 * Alias a device to a memory container
 * @obj
 * @m MemoryRegion container for the alias to be added to
 * @dev Device to be added.
 * @size of the subregion
 * @base address of the subregion
 * @name for the alias being added to the RCPU memory
 */
static void create_device_alias(Object *obj, MemoryRegion *m, DeviceState *dev,
        hwaddr size, hwaddr base, const char *name)
{
    MemoryRegion *orig = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    create_alias(obj, m, orig, size, base, name);
}

static void tt_atlantis_soc_realize(DeviceState *dev, Error **errp)
{
    TTAtlantisSoCState *s = TT_ATLANTIS_SOC(dev);
    TTAscalonClusterState *ac = &s->ascalon;
    TTRCPUClusterState *rc = &s->rcpu;
    ram_addr_t lo_ram_size, ram_size, rcpu_ram_size;
    int hart_count = ac->num_harts;

    if (!s->dram) {
        error_setg(errp, "'dram' link is not set");
        return;
    }

    if (!qdev_realize(DEVICE(&s->ascalon), NULL, errp)) {
        return;
    }

    if (s->has_rcpu) {
        /* Only create the RCPU if it is enabled */
        object_initialize_child(OBJECT(s), "rcpu", &s->rcpu, TYPE_TT_RCPU_CLUSTER);
        if (!qdev_realize(DEVICE(&s->rcpu), NULL, errp)) {
            return;
        }
    }

    ram_size = memory_region_size(s->dram);

    s->irqchip = riscv_create_aia(&ac->memory,
                                  true, TT_IMSIC_GUESTS,
                                  TT_IMSIC_STRIDE,
                                  TT_IMSIC_STRIDE,
                                  TT_IRQCHIP_NUM_SOURCES,
                                  &ac->memmap[TT_ATL_MAPLIC],
                                  &ac->memmap[TT_ATL_SAPLIC],
                                  &ac->memmap[TT_ATL_MIMSIC],
                                  &ac->memmap[TT_ATL_SIMSIC],
                                  0, 0, hart_count,
                                  TT_IRQCHIP_NUM_MSIS,
                                  TT_IRQCHIP_NUM_PRIO_BITS);

    riscv_aclint_mtimer_create(&ac->memory,
            ac->memmap[TT_ATL_ACLINT].base,
            TT_ACLINT_MTIME_SIZE,
            0, hart_count,
            TT_ACLINT_MTIMECMP,
            TT_ACLINT_MTIME,
            TT_ACLINT_TIMEBASE_FREQ, true);

    /*
     * DDR
     *
     * The high address is where RAM lives. It is always present and may be
     * up to 64GB. The low address is an alias of the first 2GB of that RAM.
     */
    if (ram_size > ac->memmap[TT_ATL_DDR_HI].size) {
        g_autofree char *sz = size_to_str(ac->memmap[TT_ATL_DDR_HI].size);
        error_setg(errp, "RAM size is too large, maximum is %s", sz);
        return;
    }

    memory_region_init_alias(&s->ram_hi, OBJECT(s), "ram.high", s->dram,
                             0, ram_size);
    memory_region_add_subregion(&ac->memory,
                                ac->memmap[TT_ATL_DDR_HI].base, &s->ram_hi);

    lo_ram_size = MIN(ram_size, ac->memmap[TT_ATL_DDR_LO].size);
    memory_region_init_alias(&s->ram_lo, OBJECT(s), "ram.low", s->dram,
                             0, lo_ram_size);
    memory_region_add_subregion(&ac->memory,
                                ac->memmap[TT_ATL_DDR_LO].base, &s->ram_lo);
    if (s->has_rcpu) {
        /*
         * The RCPU has a 1G view of lower DDR. DDR low size can be configured by
         * the command line parameters so pick the minimum between 1G and the user
         * provided size.
         */
        rcpu_ram_size = MIN(rc->memmap[TT_ATL_DDR_LO].size, lo_ram_size);
        create_alias(OBJECT(s), &rc->memory, &s->ram_lo,
                     rcpu_ram_size,
                     rc->memmap[TT_ATL_DDR_LO].base,
                     "tt-rcpu.ram");

        /* UART0, the RCPU console */
        qdev_prop_set_chr(DEVICE(&s->uart[tt_atlantis_uart_index(TT_ATL_UART0)]),
                          "chardev", serial_hd(1));
    }


    /* 4 PRCM blocks */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_RCPU]),
                        errp)) {
        return;
    }
    memory_region_add_subregion(
        &ac->memory, ac->memmap[TT_ATL_PRCM_RCPU].base,
        sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_RCPU]), 0));
    if (s->has_rcpu) {
        create_device_alias(OBJECT(s), &rc->memory,
                            DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_RCPU]),
                            rc->memmap[TT_ATL_PRCM_RCPU].size,
                            rc->memmap[TT_ATL_PRCM_RCPU].base,
                            "tt-rcpu.rcpu-prcm");
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_HSIO]),
                        errp)) {
        return;
    }
    memory_region_add_subregion(
        &ac->memory, ac->memmap[TT_ATL_PRCM_HSIO].base,
        sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_HSIO]), 0));
    if (s->has_rcpu) {
        create_device_alias(OBJECT(s), &rc->memory,
                            DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_HSIO]),
                            rc->memmap[TT_ATL_PRCM_HSIO].size,
                            rc->memmap[TT_ATL_PRCM_HSIO].base,
                            "tt-rcpu.rcpu-hsio");
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_PCIE]),
                        errp)) {
        return;
    }
    memory_region_add_subregion(
        &ac->memory, ac->memmap[TT_ATL_PRCM_PCIE].base,
        sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_PCIE]), 0));
    if (s->has_rcpu) {
        create_device_alias(OBJECT(s), &rc->memory,
                            DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_PCIE]),
                            rc->memmap[TT_ATL_PRCM_PCIE].size,
                            rc->memmap[TT_ATL_PRCM_PCIE].base,
                            "tt-rcpu.rcpu-pcie");
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_MM]),
                        errp)) {
        return;
    }
    memory_region_add_subregion(
        &ac->memory, ac->memmap[TT_ATL_PRCM_MM].base,
        sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_MM]), 0));
    if (s->has_rcpu) {
        create_device_alias(OBJECT(s), &rc->memory,
                            DEVICE(&s->prcm[TT_ATLANTIS_PRCM_DOMAIN_MM]),
                            rc->memmap[TT_ATL_PRCM_MM].size,
                            rc->memmap[TT_ATL_PRCM_MM].base,
                            "tt-rcpu.rcpu-mm");
    }

    /* UART1, the soc console */
    qdev_prop_set_chr(DEVICE(&s->uart[tt_atlantis_uart_index(TT_ATL_UART1)]),
                      "chardev", serial_hd(0));

    for (int i = 0; i < TT_ATL_NUM_UARTS; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->uart[i]);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }

        memory_region_add_subregion(&ac->memory,
                                    ac->memmap[TT_ATL_UART0 + i].base,
                                    sysbus_mmio_get_region(sbd, 0));
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(s->irqchip, TT_ATL_UART0_IRQ + i));

        if (s->has_rcpu) {
            /* Alias the devices to the rcpu memory view */
            g_autofree char *name = g_strdup_printf("tt-rcpu.uart%d", i);

            create_device_alias(OBJECT(s), &rc->memory, DEVICE(&s->uart[i]),
                                rc->memmap[TT_ATL_UART0 + i].size,
                                rc->memmap[TT_ATL_UART0 + i].base,
                                name);
        }
    }

    /* I2C */
    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->i2c[i]);

        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion(&ac->memory,
                                    ac->memmap[TT_ATL_I2C0 + i].base,
                                    sysbus_mmio_get_region(sbd, 0));
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(s->irqchip, TT_ATL_I2C0_IRQ + i));
    }
}

static const Property tt_atlantis_soc_props[] = {
    DEFINE_PROP_LINK("dram", TTAtlantisSoCState, dram,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_BOOL("has-rcpu", TTAtlantisSoCState, has_rcpu, false)
};

static void tt_atlantis_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = tt_atlantis_soc_realize;
    device_class_set_props(dc, tt_atlantis_soc_props);
    /* The SoC can only be instantiated from the machine */
    dc->user_creatable = false;
}

static void tt_atlantis_machine_init(MachineState *machine)
{
    TTAtlantisState *ams = TT_ATLANTIS_MACHINE(machine);
    TTAtlantisSoCState *s = &ams->soc;
    TTAscalonClusterState *ac = &s->ascalon;
    uint32_t num_asc_harts;

    object_initialize_child(OBJECT(machine), "soc", &ams->soc,
                            TYPE_TT_ATLANTIS_SOC);

    /*
     * The smp must include the RCPU since smp.cpus limits the total number of
     * CPUs that TCG creates. If RCPU is enabled, remove it from the hart count
     * passed to Ascalon.
     */
    num_asc_harts = machine->smp.cpus;
    if (ams->has_rcpu) {
        if (num_asc_harts <= 1) {
            error_report("smp.cpus must be at least 2 when RCPU is enabled. " \
                         "Run with ,rcpu=off to disable RCPU.");
            exit(1);
        }
        num_asc_harts--;
    }

    /* Default smp with be 9 even when RCPU is disabled */
    num_asc_harts = MIN(num_asc_harts, 8);

    object_property_set_str(OBJECT(ac), "cpu-type",
                            machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(ac), "num-harts",
                            num_asc_harts,
                            &error_abort);

    object_property_set_link(OBJECT(s), "dram", OBJECT(machine->ram),
                             &error_abort);
    object_property_set_bool(OBJECT(&ams->soc), "has-rcpu", ams->has_rcpu,
                             &error_abort);

    qdev_realize(DEVICE(&ams->soc), NULL, &error_fatal);

    /* I2C peripherals: qemu specific */
    i2c_slave_create_simple(i2c_get_bus(s, 0), TYPE_DS1338, 0x6f);
    i2c_slave_create_simple(i2c_get_bus(s, 4), "tmp105", 0x48);

    /* Load or create device tree */
    if (machine->dtb) {
        load_fdt(ams);
    } else {
        create_fdt(ams);
    }

    ams->machine_done.notify = tt_atlantis_machine_done;
    qemu_add_machine_init_done_notifier(&ams->machine_done);
}

static bool tt_atlantis_machine_get_rcpu(Object *obj, Error **errp)
{
    TTAtlantisState *s = TT_ATLANTIS_MACHINE(obj);

    return s->has_rcpu;
}

static void tt_atlantis_machine_set_rcpu(Object *obj, bool value, Error **errp)
{
    TTAtlantisState *s = TT_ATLANTIS_MACHINE(obj);

    s->has_rcpu = value;
}

static void tt_atlantis_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ObjectProperty *prop;

    mc->desc = "Tenstorrent Atlantis RISC-V SoC (Experimental)";
    mc->init = tt_atlantis_machine_init;
    /*
     * The smp count must include the RCPU since tcg limits the number of
     * threads created based on it. When rcpu=on, the number of Ascalon harts
     * created is smp.cpus - 1. When rcpu=off, the number of Ascalon harts is
     * smp.cpus.
     */
    mc->max_cpus = 9;
    mc->default_cpus = 9;
    mc->default_ram_size = 4 * GiB;
    mc->default_cpu_type = TYPE_RISCV_CPU_TT_ASCALON;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->default_ram_id = "tt_atlantis.ram";

    prop = object_class_property_add_bool(oc, "rcpu", tt_atlantis_machine_get_rcpu,
                                   tt_atlantis_machine_set_rcpu);
    object_class_property_set_description(oc, "rcpu",
                                          "Set to enable/disable modeling the RCPU.");
    object_property_set_default_bool(prop, true);

}

static const TypeInfo tt_atlantis_types[] = {
    {
        .name       = TYPE_TT_ASCALON_CLUSTER,
        .parent     = TYPE_DEVICE,
        .instance_size = sizeof(TTAscalonClusterState),
        .instance_init = tt_ascalon_cluster_init,
        .class_init = tt_ascalon_cluster_class_init,
    }, {
        .name       = TYPE_TT_RCPU_CLUSTER,
        .parent     = TYPE_DEVICE,
        .instance_size = sizeof(TTRCPUClusterState),
        .instance_init = tt_rcpu_cluster_init,
        .class_init = tt_rcpu_cluster_class_init,
    }, {
        .name       = TYPE_TT_ATLANTIS_SOC,
        .parent     = TYPE_DEVICE,
        .instance_size = sizeof(TTAtlantisSoCState),
        .instance_init = tt_atlantis_soc_init,
        .class_init = tt_atlantis_soc_class_init,
    }, {
        .name       = MACHINE_TYPE_NAME("tt-atlantis"),
        .parent     = TYPE_MACHINE,
        .class_init = tt_atlantis_machine_class_init,
        .instance_size = sizeof(TTAtlantisState),
    },
};

DEFINE_TYPES(tt_atlantis_types)
