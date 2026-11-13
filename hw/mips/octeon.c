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

#define OCTEON_CIU3_BASE            0x1010000000000ULL
#define OCTEON_CIU3_SIZE            0xb0000000ULL
#define OCTEON_CIU3_PP_RST          0x100
#define OCTEON_CIU3_PP_RST_PENDING  0x110
#define OCTEON_CIU3_NMI             0x160
#define OCTEON_CIU3_FUSE            0x1a0

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
    uint64_t rst_pp_power;
};

struct OcteonIntcState {
    OcteonPeripheralState parent_obj;
    MemoryRegion ciu3;
    uint64_t ciu3_pp_rst;
};

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

static uint64_t octeon_present_cpu_mask(OcteonState *s)
{
    return (1ULL << s->cpu_count) - 1;
}

static uint64_t octeon_secondary_cpu_mask(OcteonState *s)
{
    return octeon_present_cpu_mask(s) & ~1ULL;
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

static uint64_t octeon_ciu3_read(void *opaque, hwaddr addr, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t value;

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

    return 0;
}

static void octeon_ciu3_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    OcteonState *s = opaque;
    hwaddr reg = addr & ~7ULL;
    uint64_t old;
    unsigned int cpu;

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
        return;
    }

    if (reg == OCTEON_CIU3_NMI) {
        octeon_ciu3_nmi(s, octeon_write64(0, addr, value, size));
        return;
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

    qatomic_set(&intc->ciu3_pp_rst, octeon_secondary_cpu_mask(s));
}

static void octeon_peripheral_class_init(ObjectClass *klass,
                                          const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
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
}

static void octeon_realize_peripheral(OcteonPeripheralState *dev)
{
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
}

static void octeon_realize_peripherals(OcteonState *s)
{
    octeon_realize_peripheral(&s->intc->parent_obj);
    octeon_realize_peripheral(&s->rst->parent_obj);
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

    memory_region_init_io(&s->intc->ciu3, OBJECT(s->intc),
                          &octeon_ciu3_ops, s,
                          "octeon.ciu3", OCTEON_CIU3_SIZE);
    memory_region_add_subregion(get_system_memory(), OCTEON_CIU3_BASE,
                                &s->intc->ciu3);

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
        .class_init = octeon_rst_class_init,
    },
    {
        .name = TYPE_OCTEON_INTC,
        .parent = TYPE_OCTEON_PERIPHERAL,
        .instance_size = sizeof(OcteonIntcState),
        .class_init = octeon_intc_class_init,
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
