/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Sun-3 MMU Model
 *
 * Copyright (c) 2026
 */
#include "qemu/osdep.h"

#include "exec/cputlb.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/m68k/sun3mmu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "target/m68k/cpu.h"
#include "system/address-spaces.h"

#define SUN3_MMU_CONTEXT(addr) ((addr >> 28) & 0x7)

static uint64_t sun3_mmu_context_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    return s->context_reg;
}

static void sun3_mmu_context_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    s->context_reg = val & 0x7;
    tlb_flush(CPU(first_cpu));
}

static const MemoryRegionOps sun3_mmu_context_ops = {
    .read = sun3_mmu_context_read,
    .write = sun3_mmu_context_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
};

static uint64_t sun3_mmu_segment_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    uint32_t ctx = s->context_reg & (SUN3_MMU_CONTEXTS - 1);
    /*
     * The Segment Map index is determined by bits 17..27 of the virtual address
     */
    uint16_t seg_index = (addr >> 17) & 0x7FF;
    uint32_t index = (ctx << 11) | seg_index;
    return s->segment_map[index];
}

static void sun3_mmu_segment_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    uint32_t ctx = s->context_reg & (SUN3_MMU_CONTEXTS - 1);
    /*
     * The Segment Map index is determined by bits 17..27 of the virtual address
     */
    uint16_t seg_index = (addr >> 17) & 0x7FF;
    s->segment_map[(ctx * SUN3_MMU_SEGMENTS_PER_CONTEXT) + seg_index] =
        val & 0xFF;
    tlb_flush(CPU(first_cpu));
}

static const MemoryRegionOps sun3_mmu_segment_ops = {
    .read = sun3_mmu_segment_read,
    .write = sun3_mmu_segment_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
};

static uint64_t sun3_mmu_page_read(void *opaque, hwaddr addr, unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    uint32_t ctx = s->context_reg & (SUN3_MMU_CONTEXTS - 1);

    /*
     * The Page Map address offset contains the virtual segment AND page
     * index.
     */
    uint16_t vtr_seg = (addr >> 17) & 0x7FF;
    uint16_t vtr_page = (addr >> 13) & 0xF;
    uint32_t pmeg = s->segment_map[(ctx << 11) | vtr_seg];
    uint32_t index = (pmeg << 4) | vtr_page;
    return s->page_map[index];
}

static void sun3_mmu_page_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    uint32_t ctx = s->context_reg & (SUN3_MMU_CONTEXTS - 1);

    uint16_t vtr_seg = (addr >> 17) & 0x7FF;
    uint16_t vtr_page = (addr >> 13) & 0xF;
    uint32_t pmeg = s->segment_map[(ctx << 11) | vtr_seg];
    uint32_t index = (pmeg << 4) | vtr_page;

    if (size == 4) {
        s->page_map[index] = val;
    } else if (size == 2) {
        uint32_t shift = (addr & 2) ? 0 : 16;
        s->page_map[index] = (s->page_map[index] & ~(0xFFFF << shift)) |
                             ((val & 0xFFFF) << shift);
    } else if (size == 1) {
        uint32_t shift = (3 - (addr & 3)) * 8;
        s->page_map[index] = (s->page_map[index] & ~(0xFF << shift)) |
                             ((val & 0xFF) << shift);
    }

    tlb_flush(CPU(first_cpu));
}

static const MemoryRegionOps sun3_mmu_page_ops = {
    .read = sun3_mmu_page_read,
    .write = sun3_mmu_page_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
};

static uint64_t sun3_mmu_control_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    /* The region covers multiple 32-bit mapped registers now */
    if (addr == 0x0) {
        /*
         * The hardware diagnostic switch on the Sun-3 CPU board is checked
         * here. Setting bit 0 to 1 forces the extended memory test.
         */
        return s->enable_reg | 0x01;
    }

    /* Diagnostic LEDs */
    if (addr == 0x30000000) {
        return 0xFF; /* Typically inverted, 0xFF means all off */
    }

    qemu_log_mask(LOG_UNIMP,
                  "sun3_mmu_control_read at offset 0x%" HWADDR_PRIx
                  " (size=%u)\n",
                  addr, size);
    return 0;
}

static uint64_t sun3_mmu_buserr_read(void *opaque, hwaddr addr, unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);

    uint8_t ret = s->buserr_reg;
    s->buserr_reg = 0; /* Hardware clears on read */

    return ret;
}

static void sun3_mmu_buserr_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    s->buserr_reg = 0;
}

static const MemoryRegionOps sun3_mmu_buserr_ops = {
    .read = sun3_mmu_buserr_read,
    .write = sun3_mmu_buserr_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
};

static void sun3_mmu_control_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    Sun3MMUState *s = SUN3_MMU(opaque);
    if (addr == 0x0) {
        /* System Enable Register at 0x40000000 */
        uint8_t enable_old = s->enable_reg;
        s->enable_reg = (enable_old & 0x01) | (val & 0xFE);

        tlb_flush(CPU(first_cpu));
        return;
    }

    if (addr == 0x30000000) {
        /* Otherwise Diagnostic LEDs (e.g. 0xFF to clear) */
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "sun3_mmu_control_write at offset 0x%" HWADDR_PRIx
                  "\n", addr);
}

static const MemoryRegionOps sun3_mmu_control_ops = {
    .read = sun3_mmu_control_read,
    .write = sun3_mmu_control_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
};

static void sun3_mmu_reset(DeviceState *dev)
{
    Sun3MMUState *s = SUN3_MMU(dev);

    s->context_reg = 0;

    /*
     * On a Cold Boot, the Bus Error Register MUST be 0x00.
     * Bit 0 is NOT a Watchdog flag that must be 1. In fact, if the register
     * reads
     * non-zero, the PROM assumes it is returning from a Watchdog/Bus Error
     * Panic and attempts to dump CPU state to memory (moveml) before the
     * MMU is initialized, causing a Double Fault.
     */
    s->buserr_reg = 0x00;

    /*
     * CRITICAL: Do NOT wipe the Segment Map or Page Map!
       The Sun-3 Boot PROM relies on the physical MMU SRAM persisting across
     * Watchdog Resets so it can trace and push exception vectors back
     * into mapped physical RAM!
     */
}

static void sun3_mmu_init(Object *obj)
{
    Sun3MMUState *s = SUN3_MMU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Context Map */
    /*
     * Note: Control space regions decode the top nibble of a 32-bit address.
       The PROM uses the raw virtual address as the offset when accessing
       these regions, so they must handle sparsely distributed addresses up to
       256MB.
    */
    memory_region_init_io(&s->context_mem, obj, &sun3_mmu_context_ops, s,
                          "sun3-mmu-context", 0x10000000);
    sysbus_init_mmio(sbd, &s->context_mem);

    /* Segment Map */
    memory_region_init_io(&s->segment_mem, obj, &sun3_mmu_segment_ops, s,
                          "sun3-mmu-segment", 0x10000000);
    sysbus_init_mmio(sbd, &s->segment_mem);

    /* Page Map */
    memory_region_init_io(&s->page_mem, obj, &sun3_mmu_page_ops, s,
                          "sun3-mmu-page", 0x10000000);
    sysbus_init_mmio(sbd, &s->page_mem);

    /* Other control bits (Enable Register, Diagnostic LEDs) */
    memory_region_init_io(&s->control_mem, obj, &sun3_mmu_control_ops, s,
                          "sun3-mmu-control", 0x40000000);
    sysbus_init_mmio(sbd, &s->control_mem);

    /*
     * Bus Error Register dedicated mapping
     */
    memory_region_init_io(&s->buserr_mem, obj, &sun3_mmu_buserr_ops, s,
                          "sun3-mmu-buserr", 1);
    sysbus_init_mmio(sbd, &s->buserr_mem);

    /* DVMA IOMMU interception region */
    memory_region_init_iommu(&s->dvma_iommu, sizeof(s->dvma_iommu),
                             TYPE_SUN3_DVMA_IOMMU_MEMORY_REGION,
                             obj, "sun3-dvma", 0x1000000);
}

static void sun3_mmu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, sun3_mmu_reset);
    dc->vmsd = NULL; /* TODO: Add migration state later */
}

static IOMMUTLBEntry sun3_dvma_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                         IOMMUAccessFlags flag, int iommu_idx)
{
    Sun3MMUState *s = container_of(iommu, Sun3MMUState, dvma_iommu);
    CPUState *cs = first_cpu;
    CPUM68KState *env = cs ? cpu_env(cs) : NULL;

    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    if (!env) {
        return ret;
    }

    hwaddr physical;
    int prot;
    hwaddr page_size;
    /*
     * Lance DVMA translates 24-bit requests implicitly onto the top 16MB of
     * the 28-bit virtual Bus (0x0Fxxxxxx) tied rigidly to Context 0.
     */
    uint32_t vaddr = addr + 0x0F000000;

    int access_type = ACCESS_DATA | ACCESS_SUPER | (5 << 8);
    if (flag == IOMMU_WO || flag == IOMMU_RW) {
        access_type |= ACCESS_STORE;
    }

    uint8_t old_ctx = s->context_reg;
    s->context_reg = 0; /* Hardware forces Context 0 during DVMA */

    if (sun3mmu_get_physical_address(env, &physical, &prot, vaddr,
                                     access_type, &page_size) == 0) {
        ret.translated_addr = physical & ~(page_size - 1);
        ret.addr_mask = page_size - 1;
        if (prot & PAGE_WRITE) {
            ret.perm = IOMMU_RW;
        } else if (prot & PAGE_READ) {
            ret.perm = IOMMU_RO;
        }
    }
    s->context_reg = old_ctx; /* Restore pre-DVMA context */
    return ret;
}

static void sun3_dvma_iommu_memory_region_class_init(ObjectClass *klass,
                                                     const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);
    imrc->translate = sun3_dvma_translate;
}

static const TypeInfo sun3_dvma_iommu_memory_region_info = {
    .name = TYPE_SUN3_DVMA_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = sun3_dvma_iommu_memory_region_class_init,
};

static const TypeInfo sun3_mmu_info = {
    .name = TYPE_SUN3_MMU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Sun3MMUState),
    .instance_init = sun3_mmu_init,
    .class_init = sun3_mmu_class_init,
};

static void sun3_mmu_register_types(void)
{
    type_register_static(&sun3_mmu_info);
    type_register_static(&sun3_dvma_iommu_memory_region_info);
}

type_init(sun3_mmu_register_types)

static bool is_valid_sun3_phys(hwaddr p, ram_addr_t ram_size)
{
    if (p < ram_size) {
        return true;
    }
    if (p >= 0x0FE50000 && p <= 0x0FE7FFFF) {
        return true; /* NVRAM and OBIO RAM */
    }
    if (p >= 0x08000000 && p <= 0x08001FFF) {
        return true; /* IDPROM */
    }
    if (p >= 0x0FEF0000 && p <= 0x0FEFFFFF) {
        return true; /* PROM */
    }
    if (p >= 0x0FF00000 && p <= 0x0FF0FFFF) {
        return true; /* PROM Alias */
    }

    /* Specific OBIO devices for Sun-3/60 (Ferrari) */
    if (p >= 0x0FE00000 && p <= 0x0FE00007) {
        return true; /* ZS1 (Kbd/Mouse) */
    }
    if (p >= 0x0FE20000 && p <= 0x0FE20007) {
        return true; /* ZS0 (Serial) */
    }
    if (p >= 0x0FE40000 && p <= 0x0FE407FF) {
        return true; /* EEPROM */
    }
    if (p >= 0x0FF20000 && p <= 0x0FF201FF) {
        return true; /* LANCE Am7990 */
    }
    if (p >= 0x0FE60000 && p <= 0x0FE6003F) {
        return true; /* Timer */
    }
    if (p >= 0x0FE80000 && p <= 0x0FE8001F) {
        return true; /* Memerr */
    }
    if (p >= 0x0FEA0000 && p <= 0x0FEA0003) {
        return true; /* Intreg */
    }

    return false;
}

int sun3mmu_get_physical_address(void *env, hwaddr *physical, int *prot,
                                 vaddr address, int access_type,
                                 hwaddr *page_size)
{
    /*
     * Translate the virtual address using the Sun-3 MMU maps.
     */
    CPUM68KState *m68k_env = env;
    Sun3MMUState *s = SUN3_MMU(m68k_env->custom_mmu_opaque);

    uint8_t context;
    uint16_t pmeg = 0;
    uint32_t pte;
    uint32_t pte_index = 0;
    uint32_t pte_offset = 0;
    uint32_t phys_addr;

    /*
     * access_type from m68k TCG:
       ACCESS_CODE (0x10), ACCESS_DATA (0x20)
        * ACCESS_SUPER (0x01) *
        */
    bool is_write = access_type & ACCESS_STORE;
    bool is_supervisor = access_type & ACCESS_SUPER;

    *page_size = TARGET_PAGE_SIZE;

    /*
     * QEMU Pipeline Prefetch Workaround:
     * The Sun-3 PROM executes `movesb %d0, 0x40000000` out of ROM (0x0FEFxxxx)
     * to enable the MMU (`enable_reg |= 0x80`). On real M68020 hardware, the
     * subsequent instruction(s) have already been prefetched while the MMU
     * was off. QEMU attempts to fetch the next sequential instruction
     * synchronously with the MMU fully active. Because the PROM has not mapped
     * 0x0FEF0000 in the Page Table (it only maps Virtual 0x00000000 to
     * the ROM),
     * QEMU triggers an immediate Translation Fault Exception Loop! We must
     * manually bless instruction fetches originating mechanically from the
     * physical ROM space to emulate the prefetch cache. *
     */
    if ((access_type & ACCESS_CODE) &&
        (address >= 0x0FEF0000 && address <= 0x0FEFFFFF)) {
        *physical = address;
        *prot = PAGE_READ | PAGE_EXEC;
        return 0;
    }

    /*
     * Boot Mode PROM Bypass:
       If the Not-Boot bit (0x80) in the Enable Register is clear (System is in
       Boot State): ONLY Instruction Fetches bypass the MMU mapping mechanism!
       Data accesses and Stack Pushes (such as the Exception Frame push to
       0x0FEEFFFE) proceed through the MMU mapped tables normally because the
        * PROM sets up its stack logically! *
        */



    qemu_log_mask(CPU_LOG_MMU,
                  "[SUN3MMU] get_physical_address(0x%08" VADDR_PRIx
                  ") enable_reg=0x%02x\n",
                  address, s->enable_reg);

    /*
     * Sun-3 Hardware Architectural Demultiplexer:
     * The M68020 emulator now seamlessly passes the Source/Destination Function
     * Code inside the top 8 bits of the `access_type` bitmask, directly
     * supplied
     * via an isolation index in the QEMU TCG pipeline. This mirrors
     * how the physical M68K processor provides 3 explicit FC pins in
     * addition to
     * the 32-bit physical address bus!
     *
     * If the extracted FC equals 3 (Control Space / Hardware Registers), it
     * NEVER enters the MMU Segment Map. It is universally 1:1 mapped to
     * physical memory for raw HW device configuration (0x60000000, 0x10000000)!
     */
    uint8_t true_fc = (access_type >> 8) & 0x07;
    if (true_fc == 3) {
        /*
         * Direct Physical Bypass Mapping to discrete SysBus devices.
         * The top nibble of the virtual address selects the Control Space
         * target. All lower bits are aliases.
         */
        uint32_t device_base = address & 0x70000000;

        if (device_base == 0x00000000) {
            /*
             * IDPROM is logically at 0x0 and occupies 32 bytes in hardware. We
             * shift it to 0x08000000 linearly in QEMU and force a 5-bit wrap
             * so it safely avoids physical Main RAM/ROM collisions! *
             */
            *physical = 0x08000000 | (address & 0x1F);
        } else {
            switch (device_base) {
            case 0x10000000: /* Page Map */
                /*
                 * Hardware Page Map physically contains 256 PMEGs *
                 * 16 PTEs * 4 bytes = 16,384 bytes
                 */
                *physical = 0xA0000000 | (address & 0x0FFFFFFF);
                break;
            case 0x20000000: /* Segment Map */
                /*
                 * Hardware Segment Map structurally is 8 Contexts *
                 * 2048 segments * 1 byte = 16,384 bytes
                 */
                *physical = 0x90000000 | (address & 0x0FFFFFFF);
                break;
            case 0x30000000: /* Context Register */
                /*
                 * Context register physically uniquely masks natively
                 * strictly functionally inside hardware
                 */
                *physical = 0x80000000 | (address & 0x07);
                break;
            case 0x40000000: /* System Enable */
                *physical = 0xB0000000 | (address & 0x0FFFFFFF);
                break;
            case 0x60000000: /* Bus Error Register */
                *physical = 0xC0000000 | (address & 0x0FFFFFFF);
                break;
            case 0x70000000:
                /* Diagnostic Register (Aliases into Enable mem region) */
                *physical = 0xB0000000ULL + 0x30000000ULL +
                            (address & 0x0FFFFFFF);
                break;
            default:
                s->buserr_reg |= 0x20; /* Timeout */
                return 1;
            }
      }

      *prot = PAGE_READ | PAGE_WRITE;
      *page_size = SUN3_PAGE_SIZE;

      qemu_log_mask(
          CPU_LOG_MMU,
          "[SUN3MMU] TRUE FC=3 CONTROL SPACE DECODE: vaddr=0x%08" VADDR_PRIx
          " mapped directly to physical 0x%08x\n",
          address, (unsigned int)*physical);

      return 0;
    }

    /*
     * For all standard memory operations, the Sun-3 physically masks the
     * Address Bus to 28 virtual bits before hitting the MMU Arrays.
     */
    address &= 0x0FFFFFFF;

    if (!(s->enable_reg & 0x80)) {
        /* Boot State: Not-Boot bit (0x80) is CLEAR */

        /*
         * Address < 0x01000000 && Supervisor Program:
         *    Bypass MMU and map to the physical PROM (0x0FEF0000).
         */
        if (true_fc == 6 && (address < 0x01000000 ||
                             (address >= 0x0FEF0000 && address < 0x0FF00000))) {
            *physical = 0x0FEF0000 | (address & 0x0001FFFF);
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            *page_size = SUN3_PAGE_SIZE;
            return 0;
        }
    }

    context = s->context_reg & (SUN3_MMU_CONTEXTS - 1);

    /* Segment map lookup: top 11 bits of 28-bit virtual address (bits 17-27) */
    uint32_t seg_index = (address >> 17) & (SUN3_MMU_SEGMENTS_PER_CONTEXT - 1);
    pmeg = s->segment_map[(context * SUN3_MMU_SEGMENTS_PER_CONTEXT) +
                          seg_index];

    /* Page map lookup: bits 13-16 of virtual address */
    pte_index = (address >> SUN3_PAGE_SHIFT) & (SUN3_MMU_PTE_PER_PMEG - 1);
    pte_offset = (pmeg * SUN3_MMU_PTE_PER_PMEG) + pte_index;
    pte = s->page_map[pte_offset];

    /* Update PTE Accessed/Modified bits */
    if (pte & SUN3_PTE_VALID) {
        uint32_t new_pte = pte | SUN3_PTE_REF;
        if (is_write) {
            new_pte |= SUN3_PTE_MOD;
        }
        if (new_pte != pte) {
            s->page_map[pte_offset] = new_pte;
        }
    }

    if (!(pte & SUN3_PTE_VALID)) {
        s->buserr_reg |= 0x80; /* Invalid */
        return 1;              /* Translation fault */
    }

    /* Protection check */
    uint8_t mmu_prot = (pte >> 29) & 3;

    if (!is_supervisor && (mmu_prot & 1)) {
        s->buserr_reg |= 0x40; /* Protection */
        return 1;              /* User access to supervisor page */
    }

    *prot = PAGE_READ | PAGE_EXEC;
    if (mmu_prot & 2) {
        if (is_write || (pte & SUN3_PTE_MOD)) {
            *prot |= PAGE_WRITE;
        }
    }

    if (is_write && !(mmu_prot & 2)) {
        s->buserr_reg |= 0x40; /* Protection */
        return 1;
    }

    /*
     * Extract physical address. The top 15 bits come from the PTE's
     * PGFRAME. Bottom 13 bits (0x1FFF) come directly from the virtual
     * address.
     */
    phys_addr = ((pte & SUN3_PTE_PGFRAME) << SUN3_PAGE_SHIFT) |
                (address & (SUN3_PAGE_SIZE - 1));

    /*
     * Address space Mapping reference:
     *   - OBMEM:   0x00000000 (Follows native Map)
     *   - OBIO:    0x0FE00000 (Relocated above typical 24MB RAM)
     *   - VME_D16: 0x40000000 (Relocated out of bounds)
     *   - VME_D32: 0x50000000
     */
    uint32_t pgbase = ((pte & SUN3_PTE_PGFRAME) << SUN3_PAGE_SHIFT);
    uint32_t pgtype = (pte & SUN3_PTE_PGTYPE) >> 26;

    /*
     * Sun-3 Hardware Quirk:
     * The Boot PROM maps Virtual `0x00000000` to its ROM header using OBIO
     * Page `0x80` (`0xC4000080`). Native hardware intercepts OBIO offset
     * `0x100000` and transparently aliases it back to OBMEM PROM
     * (`0x0FEF0000`) using the virtual address to index the ROM! *
     */
    if (pgtype == SUN3_PGTYPE_OBIO &&
        (pgbase >= 0x100000 && pgbase < 0x120000)) {
        phys_addr = 0x0FEF0000 | (address & 0x1FFFF);
    } else {
        switch (pgtype) {
        case SUN3_PGTYPE_OBMEM:
            break;
        case SUN3_PGTYPE_OBIO:
            phys_addr += 0x0FE00000;
            break;
        case SUN3_PGTYPE_VME_D16:
            phys_addr += 0x40000000;
            break;
        case SUN3_PGTYPE_VME_D32:
            phys_addr += 0x50000000;
            break;
        }
    }

    /* NXM (Non-Existent Memory) Bounds Checking */
    if (!is_valid_sun3_phys(phys_addr, current_machine->ram_size)) {
        s->buserr_reg |= 0x20; /* Timeout */
        return 1;
    }

    /*
     * The QEMU TLB works in 4KB frames, not Sun-3's 8KB native frames.
     * We MUST explicitly append the intra-page offset within the 8KB page,
     * otherwise accesses to the upper 4KB (e.g. 0x1000, 0x3000) will be
     * truncated and overwrite the physical memory of the lower 4KB!
     */
    *physical = phys_addr | (address & (SUN3_PAGE_SIZE - 1));

    *page_size = TARGET_PAGE_SIZE;

    return 0; /* 0 = success, no fault */
}
