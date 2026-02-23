/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "hw/intc/arm_gicv5.h"
#include "hw/intc/arm_gicv5_stream.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"
#include "migration/blocker.h"

OBJECT_DEFINE_TYPE(GICv5, gicv5, ARM_GICV5, ARM_GICV5_COMMON)

static const char *domain_name[] = {
    [GICV5_ID_S] = "Secure",
    [GICV5_ID_NS] = "NonSecure",
    [GICV5_ID_EL3] = "EL3",
    [GICV5_ID_REALM] = "Realm",
};

static const char *inttype_name(GICv5IntType t)
{
    /*
     * We have to be more cautious with getting human readable names
     * for a GICv5IntType for trace strings than we do with the
     * domain enum, because here the value can come from a guest
     * register field.
     */
    static const char *names[] = {
        [GICV5_PPI] = "PPI",
        [GICV5_LPI] = "LPI",
        [GICV5_SPI] = "SPI",
    };
    if (t >= ARRAY_SIZE(names) || !names[t]) {
        return "RESERVED";
    }
    return names[t];
}

REG32(IRS_IDR0, 0x0)
    FIELD(IRS_IDR0, INT_DOM, 0, 2)
    FIELD(IRS_IDR0, PA_RANGE, 2, 5)
    FIELD(IRS_IDR0, VIRT, 6, 1)
    FIELD(IRS_IDR0, ONE_N, 7, 1)
    FIELD(IRS_IDR0, VIRT_ONE_N, 8, 1)
    FIELD(IRS_IDR0, SETLPI, 9, 1)
    FIELD(IRS_IDR0, MEC, 10, 1)
    FIELD(IRS_IDR0, MPAM, 11, 1)
    FIELD(IRS_IDR0, SWE, 12, 1)
    FIELD(IRS_IDR0, IRSID, 16, 16)

REG32(IRS_IDR1, 0x4)
    FIELD(IRS_IDR1, PE_CNT, 0, 16)
    FIELD(IRS_IDR1, IAFFID_BITS, 16, 4)
    FIELD(IRS_IDR1, PRI_BITS, 20, 3)

REG32(IRS_IDR2, 0x8)
    FIELD(IRS_IDR2, ID_BITS, 0, 5)
    FIELD(IRS_IDR2, LPI, 5, 1)
    FIELD(IRS_IDR2, MIN_LPI_ID_BITS, 6, 4)
    FIELD(IRS_IDR2, IST_LEVELS, 10, 1)
    FIELD(IRS_IDR2, IST_L2SZ, 11, 3)
    FIELD(IRS_IDR2, IST_MD, 14, 1)
    FIELD(IRS_IDR2, ISTMD_SZ, 15, 5)

REG32(IRS_IDR3, 0xc)
    FIELD(IRS_IDR3, VMD, 0, 1)
    FIELD(IRS_IDR3, VMD_SZ, 1, 4)
    FIELD(IRS_IDR3, VM_ID_BITS, 5, 5)
    FIELD(IRS_IDR3, VMT_LEVELS, 10, 1)

REG32(IRS_IDR4, 0x10)
    FIELD(IRS_IDR4, VPED_SZ, 0, 6)
    FIELD(IRS_IDR4, VPE_ID_BITS, 6, 4)

REG32(IRS_IDR5, 0x14)
    FIELD(IRS_IDR5, SPI_RANGE, 0, 25)

REG32(IRS_IDR6, 0x18)
    FIELD(IRS_IDR6, SPI_IRS_RANGE, 0, 25)

REG32(IRS_IDR7, 0x1c)
    FIELD(IRS_IDR7, SPI_BASE, 0, 24)

REG32(IRS_IIDR, 0x40)
    FIELD(IRS_IIDR, IMPLEMENTER, 0, 12)
    FIELD(IRS_IIDR, REVISION, 12, 4)
    FIELD(IRS_IIDR, VARIANT, 16, 4)
    FIELD(IRS_IIDR, PRODUCTID, 20, 12)

REG32(IRS_AIDR, 0x44)
    FIELD(IRS_AIDR, ARCHMINORREV, 0, 4)
    FIELD(IRS_AIDR, ARCHMAJORREV, 4, 4)
    FIELD(IRS_AIDR, COMPONENT, 8, 4)

REG32(IRS_CR0, 0x80)
    FIELD(IRS_CR0, IRSEN, 0, 1)
    FIELD(IRS_CR0, IDLE, 1, 1)

REG32(IRS_CR1, 0x84)
    FIELD(IRS_CR1, SH, 0, 2)
    FIELD(IRS_CR1, OC, 2, 2)
    FIELD(IRS_CR1, IC, 4, 2)
    FIELD(IRS_CR1, IST_RA, 6, 1)
    FIELD(IRS_CR1, IST_WA, 7, 1)
    FIELD(IRS_CR1, VMT_RA, 8, 1)
    FIELD(IRS_CR1, VMT_WA, 9, 1)
    FIELD(IRS_CR1, VPET_RA, 10, 1)
    FIELD(IRS_CR1, VPET_WA, 11, 1)
    FIELD(IRS_CR1, VMD_RA, 12, 1)
    FIELD(IRS_CR1, VMD_WA, 13, 1)
    FIELD(IRS_CR1, VPED_RA, 14, 1)
    FIELD(IRS_CR1, VPED_WA, 15, 1)

REG32(IRS_SYNCR, 0xc0)
    FIELD(IRS_SYNCR, SYNC, 31, 1)

REG32(IRS_SYNC_STATUSR, 0xc4)
    FIELD(IRS_SYNC_STATUSR, IDLE, 0, 1)

REG64(IRS_SPI_VMR, 0x100)
    FIELD(IRS_SPI_VMR, VM_ID, 0, 16)
    FIELD(IRS_SPI_VMR, VIRT, 63, 1)

REG32(IRS_SPI_SELR, 0x108)
    FIELD(IRS_SPI_SELR, ID, 0, 24)

REG32(IRS_SPI_DOMAINR, 0x10c)
    FIELD(IRS_SPI_DOMAINR, DOMAIN, 0, 2)

REG32(IRS_SPI_RESAMPLER, 0x110)
    FIELD(IRS_SPI_RESAMPLER, SPI_ID, 0, 24)

REG32(IRS_SPI_CFGR, 0x114)
    FIELD(IRS_SPI_CFGR, TM, 0, 1)

REG32(IRS_SPI_STATUSR, 0x118)
    FIELD(IRS_SPI_STATUSR, IDLE, 0, 1)
    FIELD(IRS_SPI_STATUSR, V, 1, 1)

REG32(IRS_PE_SELR, 0x140)
    FIELD(IRS_PE_SELR, IAFFID, 0, 16)

REG32(IRS_PE_STATUSR, 0x144)
    FIELD(IRS_PE_STATUSR, IDLE, 0, 1)
    FIELD(IRS_PE_STATUSR, V, 1, 1)
    FIELD(IRS_PE_STATUSR, ONLINE, 2, 1)

REG32(IRS_PE_CR0, 0x148)
    FIELD(IRS_PE_CR0, DPS, 0, 1)

REG64(IRS_IST_BASER, 0x180)
    FIELD(IRS_IST_BASER, VALID, 0, 1)
    FIELD(IRS_IST_BASER, ADDR, 6, 50)

REG32(IRS_IST_CFGR, 0x190)
    FIELD(IRS_IST_CFGR, LPI_ID_BITS, 0, 5)
    FIELD(IRS_IST_CFGR, L2SZ, 5, 2)
    FIELD(IRS_IST_CFGR, ISTSZ, 7, 2)
    FIELD(IRS_IST_CFGR, STRUCTURE, 16, 1)

REG32(IRS_IST_STATUSR, 0x194)
    FIELD(IRS_IST_STATUSR, IDLE, 0, 1)

REG32(IRS_MAP_L2_ISTR, 0x1c0)
    FIELD(IRS_MAP_L2_ISTR, ID, 0, 24)

REG64(IRS_VMT_BASER, 0x200)
    FIELD(IRS_VMT_BASER, VALID, 0, 1)
    FIELD(IRS_VMT_BASER, ADDR, 3, 53)

REG32(IRS_VMT_CFGR, 0x210)
    FIELD(IRS_VMT_CFGR, VM_ID_BITS, 0, 5)
    FIELD(IRS_VMT_CFGR, STRUCTURE, 16, 1)

REG32(IRS_VMT_STATUSR, 0x124)
    FIELD(IRS_VMT_STATUSR, IDLE, 0, 1)

REG64(IRS_VPE_SELR, 0x240)
    FIELD(IRS_VPE_SELR, VM_ID, 0, 16)
    FIELD(IRS_VPE_SELR, VPE_ID, 32, 16)
    FIELD(IRS_VPE_SELR, S, 63, 1)

REG64(IRS_VPE_DBR, 0x248)
    FIELD(IRS_VPE_DBR, INTID, 0, 24)
    FIELD(IRS_VPE_DBR, DBPM, 32, 5)
    FIELD(IRS_VPE_DBR, REQ_DB, 62, 1)
    FIELD(IRS_VPE_DBR, DBV, 63, 1)

REG32(IRS_VPE_HPPIR, 0x250)
    FIELD(IRS_VPE_HPPIR, ID, 0, 24)
    FIELD(IRS_VPE_HPPIR, TYPE, 29, 3)
    FIELD(IRS_VPE_HPPIR, HPPIV, 32, 1)

REG32(IRS_VPE_CR0, 0x258)
    FIELD(IRS_VPE_CR0, DPS, 0, 1)

REG32(IRS_VPE_STATUSR, 0x25c)
    FIELD(IRS_VPE_STATUSR, IDLE, 0, 1)
    FIELD(IRS_VPE_STATUSR, V, 1, 1)

REG64(IRS_VM_DBR, 0x280)
    FIELD(IRS_VM_DBR, VPE_ID, 0, 16)
    FIELD(IRS_VM_DBR, EN, 63, 1)

REG32(IRS_VM_SELR, 0x288)
    FIELD(IRS_VM_SELR, VM_ID, 0, 16)

REG32(IRS_VM_STATUSR, 0x28c)
    FIELD(IRS_VM_STATUSR, IDLE, 0, 1)
    FIELD(IRS_VM_STATUSR, V, 1, 1)

REG64(IRS_VMAP_L2_VMTR, 0x2c0)
    FIELD(IRS_VMAP_L2_VMTR, VM_ID, 0, 16)
    FIELD(IRS_VMAP_L2_VMTR, M, 63, 1)

REG64(IRS_VMAP_VMR, 0x2c8)
    FIELD(IRS_VMAP_VMR, VM_ID, 0, 16)
    FIELD(IRS_VMAP_VMR, U, 62, 1)
    FIELD(IRS_VMAP_VMR, M, 63, 1)

REG64(IRS_VMAP_VISTR, 0x2d0)
    FIELD(IRS_VMAP_VISTR, TYPE, 29, 3)
    FIELD(IRS_VMAP_VISTR, VM_ID, 32, 16)
    FIELD(IRS_VMAP_VISTR, U, 62, 1)
    FIELD(IRS_VMAP_VISTR, M, 63, 1)

REG64(IRS_VMAP_L2_VISTR, 0x2d8)
    FIELD(IRS_VMAP_L2_VISTR, ID, 0, 24)
    FIELD(IRS_VMAP_L2_VISTR, TYPE, 29, 3)
    FIELD(IRS_VMAP_L2_VISTR, VM_ID, 32, 16)
    FIELD(IRS_VMAP_L2_VISTR, M, 63, 1)

REG64(IRS_VMAP_VPER, 0x2e0)
    FIELD(IRS_VMAP_VPER, VPE_ID, 0, 16)
    FIELD(IRS_VMAP_VPER, VM_ID, 32, 16)
    FIELD(IRS_VMAP_VPER, M, 63, 1)

REG64(IRS_SAVE_VMR, 0x300)
    FIELD(IRS_SAVE_VMR, VM_ID, 0, 16)
    FIELD(IRS_SAVE_VMR, Q, 62, 1)
    FIELD(IRS_SAVE_VMR, S, 63, 1)

REG32(IRS_SAVE_VM_STATUSR, 0x308)
    FIELD(IRS_SAVE_VM_STATUSR, IDLE, 0, 1)
    FIELD(IRS_SAVE_VM_STATUSR, Q, 1, 1)

REG32(IRS_MEC_IDR, 0x340)
    FIELD(IRS_MEC_IDR, MECIDSIZE, 0, 4)

REG32(IRS_MEC_MECID_R, 0x344)
    FIELD(IRS_MEC_MICID_R, MECID, 0, 16)

REG32(IRS_MPAM_IDR, 0x380)
    FIELD(IRS_MPAM_IDR, PARTID_MAX, 0, 16)
    FIELD(IRS_MPAM_IDR, PMG_MAX, 16, 8)
    FIELD(IRS_MPAM_IDR, HAS_MPAM_SP, 24, 1)

REG32(IRS_MPAM_PARTID_R, 0x384)
    FIELD(IRS_MPAM_IDR, PARTID, 0, 16)
    FIELD(IRS_MPAM_IDR, PMG, 16, 8)
    FIELD(IRS_MPAM_IDR, MPAM_SP, 24, 2)
    FIELD(IRS_MPAM_IDR, IDLE, 31, 1)

REG64(IRS_SWERR_STATUSR, 0x3c0)
    FIELD(IRS_SWERR_STATUSR, V, 0, 1)
    FIELD(IRS_SWERR_STATUSR, S0V, 1, 1)
    FIELD(IRS_SWERR_STATUSR, S1V, 2, 1)
    FIELD(IRS_SWERR_STATUSR, OF, 3, 1)
    FIELD(IRS_SWERR_STATUSR, EC, 16, 8)
    FIELD(IRS_SWERR_STATUSR, IMP_EC, 24, 8)

REG64(IRS_SWERR_SYNDROMER0, 0x3c8)
    FIELD(IRS_SWERR_SYNDROMER0, VM_ID, 0, 16)
    FIELD(IRS_SWERR_SYNDROMER0, ID, 32, 24)
    FIELD(IRS_SWERR_SYNDROMER0, TYPE, 60, 3)
    FIELD(IRS_SWERR_SYNDROMER0, VIRTUAL, 63, 1)

REG64(IRS_SWERR_SYNDROMER1, 0x3d0)
    FIELD(IRS_SWERR_SYNDROMER2, ADDR, 3, 53)

FIELD(L1_ISTE, VALID, 0, 1)
FIELD(L1_ISTE, L2_ADDR, 12, 44)

FIELD(L2_ISTE, PENDING, 0, 1)
FIELD(L2_ISTE, ACTIVE, 1, 1)
FIELD(L2_ISTE, HM, 2, 1)
FIELD(L2_ISTE, ENABLE, 3, 1)
FIELD(L2_ISTE, IRM, 4, 1)
FIELD(L2_ISTE, HWU, 9, 2)
FIELD(L2_ISTE, PRIORITY, 11, 5)
FIELD(L2_ISTE, IAFFID, 16, 16)

static MemTxAttrs irs_txattrs(GICv5Common *cs, GICv5Domain domain)
{
    /*
     * Return a MemTxAttrs to use for IRS memory accesses.
     * IRS_CR1 has the usual Arm cacheability/shareability attributes,
     * but QEMU doesn't care about those. All we need to specify here
     * is the correct security attributes, which depend on the
     * interrupt domain. Conveniently, our GICv5Domain encoding matches
     * the ARMSecuritySpace one (because both follow an architecturally
     * specified field). The exception is that the EL3 domain must
     * be Secure instead of Root if we don't implement Realm.
     */
    if (domain == GICV5_ID_EL3 &&
        !gicv5_domain_implemented(cs, GICV5_ID_REALM)) {
        domain = GICV5_ID_S;
    }
    return (MemTxAttrs) {
        .space = domain,
        .secure = domain == GICV5_ID_S || domain == GICV5_ID_EL3,
    };
}

static hwaddr l1_iste_addr(GICv5Common *cs, const GICv5ISTConfig *cfg,
                           uint32_t id)
{
    /*
     * In a 2-level IST configuration, return the address of the L1
     * IST entry for this interrupt ID.  The bottom l2_idx_bits of the
     * ID value are the index into the L2 table, and the higher bits
     * of the ID index the L1 table.
     */
    uint32_t l1_index = id >> cfg->l2_idx_bits;
    return cfg->base + (l1_index * 8);
}

static bool get_l2_iste_addr(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             uint32_t id, hwaddr *l2_iste_addr)
{
    /*
     * Get the address of the L2 interrupt state table entry for
     * this interrupt. On success, fill in l2_iste_addr and return true.
     * On failure, return false.
     */
    hwaddr l2_base;

    if (!cfg->valid) {
        return false;
    }

    if (id >= (1 << cfg->id_bits)) {
        return false;
    }

    if (cfg->structure) {
        /*
         * 2-level table: read the L1 IST. The bottom l2_idx_bits
         * of the ID value are the index into the L2 table, and
         * the higher bits of the ID index the L1 table. There is
         * always at least one L1 table entry.
         */
        hwaddr l1_addr = l1_iste_addr(cs, cfg, id);
        uint64_t l1_iste;
        MemTxResult res;

        l1_iste = address_space_ldq_le(&cs->dma_as, l1_addr,
                                       cfg->txattrs, &res);
        if (res != MEMTX_OK) {
            /* Reportable with EC=0x01 if sw error reporting implemented */
            qemu_log_mask(LOG_GUEST_ERROR, "L1 ISTE lookup failed for ID 0x%x"
                          " at physical address 0x" HWADDR_FMT_plx "\n",
                          id, l1_addr);
            return false;
        }
        if (!FIELD_EX64(l1_iste, L1_ISTE, VALID)) {
            return false;
        }
        l2_base = l1_iste & R_L1_ISTE_L2_ADDR_MASK;
        id = extract32(id, 0, cfg->l2_idx_bits);
    } else {
        /* 1-level table */
        l2_base = cfg->base;
    }

    *l2_iste_addr = l2_base + (id * cfg->istsz);
    return true;
}

static bool read_l2_iste_mem(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             hwaddr addr, uint32_t *l2_iste)
{
    MemTxResult res;
    *l2_iste = address_space_ldl_le(&cs->dma_as, addr, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        /* Reportable with EC=0x02 if sw error reporting implemented */
        qemu_log_mask(LOG_GUEST_ERROR, "L2 ISTE read failed at physical "
                      "address 0x" HWADDR_FMT_plx "\n", addr);
    }
    return res == MEMTX_OK;
}

static bool write_l2_iste_mem(GICv5Common *cs, const GICv5ISTConfig *cfg,
                              hwaddr addr, uint32_t l2_iste)
{
    MemTxResult res;
    address_space_stl_le(&cs->dma_as, addr, l2_iste, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        /* Reportable with EC=0x02 if sw error reporting implemented */
        qemu_log_mask(LOG_GUEST_ERROR, "L2 ISTE write failed at physical "
                      "address 0x" HWADDR_FMT_plx "\n", addr);
    }
    return res == MEMTX_OK;
}

/*
 * This is returned by get_l2_iste() and has everything we
 * need to do the writeback of the L2 ISTE word in put_l2_iste().
 * Currently the get/put functions always directly do guest memory
 * reads and writes to update the L2 ISTE. In a future commit we
 * will add support for a cache of some of the ISTE data in a
 * local hashtable; the APIs are designed with that in mind.
 */
typedef struct L2_ISTE_Handle {
    hwaddr l2_iste_addr;
    uint32_t l2_iste;
} L2_ISTE_Handle;

static uint32_t *get_l2_iste(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             uint32_t id, L2_ISTE_Handle *h)
{
    /*
     * Find the L2 ISTE for the interrupt @id.
     *
     * We return a pointer to the ISTE: the caller can freely
     * read and modify the uint64_t pointed to to update the ISTE.
     * If the caller modifies the L2 ISTE word, it must call
     * put_l2_iste(), passing it @h, to write back the ISTE.
     * If the caller is only reading the L2 ISTE, it does not need
     * to call put_l2_iste().
     *
     * We fill in @h with information needed for put_l2_iste().
     *
     * If the ISTE could not be read (typically because of a
     * memory error), return NULL.
     */
    if (!get_l2_iste_addr(cs, cfg, id, &h->l2_iste_addr) ||
        !read_l2_iste_mem(cs, cfg, h->l2_iste_addr, &h->l2_iste)) {
        return NULL;
    }
    return &h->l2_iste;
}

static void put_l2_iste(GICv5Common *cs, const GICv5ISTConfig *cfg,
                        L2_ISTE_Handle *h)
{
    /*
     * Write back the modified L2_ISTE word found with get_l2_iste().
     * Once this has been called the L2_ISTE_Handle @h and the
     * pointer to the L2 ISTE word are no longer valid.
     */
    write_l2_iste_mem(cs, cfg, h->l2_iste_addr, h->l2_iste);
}

void gicv5_set_priority(GICv5Common *cs, uint32_t id,
                        uint8_t priority, GICv5Domain domain,
                        GICv5IntType type, bool virtual)
{
    const GICv5ISTConfig *cfg;
    GICv5 *s = ARM_GICV5(cs);
    uint32_t *l2_iste_p;
    L2_ISTE_Handle h;

    trace_gicv5_set_priority(domain_name[domain], inttype_name(type), virtual,
                             id, priority);
    /* We must ignore unimplemented low-order priority bits */
    priority &= MAKE_64BIT_MASK(5 - QEMU_GICV5_PRI_BITS, QEMU_GICV5_PRI_BITS);

    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_priority: tried to set "
                      "priority of a virtual interrupt\n");
        return;
    }
    if (type != GICV5_LPI) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_priority: tried to set "
                      "priority of bad interrupt type %d\n", type);
        return;
    }
    cfg = &s->phys_lpi_config[domain];
    l2_iste_p = get_l2_iste(cs, cfg, id, &h);
    if (!l2_iste_p) {
        return;
    }
    *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, PRIORITY, priority);
    put_l2_iste(cs, cfg, &h);
}

static void irs_ist_baser_write(GICv5 *s, GICv5Domain domain, uint64_t value)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
        /* If VALID is set, ADDR is RO and we can only update VALID */
        bool valid = FIELD_EX64(value, IRS_IST_BASER, VALID);
        if (valid) {
            /* Ignore 1->1 transition */
            return;
        }
        cs->irs_ist_baser[domain] = FIELD_DP64(cs->irs_ist_baser[domain],
                                               IRS_IST_BASER, VALID, valid);
        s->phys_lpi_config[domain].valid = false;
        trace_gicv5_ist_invalid(domain_name[domain]);
        return;
    }
    cs->irs_ist_baser[domain] = value;

    if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
        /*
         * If the guest just set VALID then capture data into config struct,
         * sanitize the reserved values, and expand fields out into byte counts.
         */
        GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        uint8_t istbits, l2bits, l2_idx_bits;
        uint8_t id_bits = FIELD_EX64(cs->irs_ist_cfgr[domain],
                                     IRS_IST_CFGR, LPI_ID_BITS);
        id_bits = MIN(MAX(id_bits, QEMU_GICV5_MIN_LPI_ID_BITS), QEMU_GICV5_ID_BITS);

        switch (FIELD_EX64(cs->irs_ist_cfgr[domain], IRS_IST_CFGR, ISTSZ)) {
        case 0:
        case 3: /* reserved: acts like the minimum required size */
            istbits = 2;
            break;
        case 1:
            istbits = 3;
            break;
        case 2:
            istbits = 4;
            break;
        default:
            g_assert_not_reached();
        }
        switch (FIELD_EX64(cs->irs_ist_cfgr[domain], IRS_IST_CFGR, L2SZ)) {
        case 0:
        case 3: /* reserved; CONSTRAINED UNPREDICTABLE */
            l2bits = 12; /* 4K: 12 bits */
            break;
        case 1:
            l2bits = 14; /* 16K: 14 bits */
            break;
        case 2:
            l2bits = 16; /* 64K: 16 bits */
            break;
        default:
            g_assert_not_reached();
        }
        /*
         * Calculate how many bits of an ID index the L2 table
         * (e.g. if we need 14 bits to index each byte in a 16K L2 table,
         * but each entry is 4 bytes wide then we need 14 - 2 = 12 bits
         * to index an entry in the table).
         */
        l2_idx_bits = l2bits - istbits;
        cfg->base = cs->irs_ist_baser[domain] & R_IRS_IST_BASER_ADDR_MASK;
        cfg->txattrs = irs_txattrs(cs, domain),
        cfg->id_bits = id_bits;
        cfg->istsz = 1 << istbits;
        cfg->l2_idx_bits = l2_idx_bits;
        cfg->structure = FIELD_EX64(cs->irs_ist_cfgr[domain],
                                    IRS_IST_CFGR, STRUCTURE);
        cfg->valid = true;
        trace_gicv5_ist_valid(domain_name[domain], cfg->base, cfg->id_bits,
                              cfg->l2_idx_bits, cfg->istsz, cfg->structure);
    }
}

static bool config_readl(GICv5 *s, GICv5Domain domain, hwaddr offset,
                         uint64_t *data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);
    uint32_t v = 0;

    switch (offset) {
    case A_IRS_IDR0:
        v = cs->irs_idr0;
        /* INT_DOM reports the domain this register is for */
        v = FIELD_DP32(v, IRS_IDR0, INT_DOM, domain);
        if (domain != GICV5_ID_REALM) {
            /* MEC field RES0 except for the Realm domain */
            v &= ~R_IRS_IDR0_MEC_MASK;
        }
        if (domain == GICV5_ID_EL3) {
            /* VIRT is RES0 for EL3 domain */
            v &= ~R_IRS_IDR0_VIRT_MASK;
        }
        return true;

    case A_IRS_IDR1:
        *data = cs->irs_idr1;
        return true;

    case A_IRS_IDR2:
        *data = cs->irs_idr2;
        return true;

    case A_IRS_IDR3:
        /* In EL3 IDR0.VIRT is 0 so this is RES0 */
        *data = domain == GICV5_ID_EL3 ? 0 : cs->irs_idr3;
        return true;

    case A_IRS_IDR4:
        /* In EL3 IDR0.VIRT is 0 so this is RES0 */
        *data = domain == GICV5_ID_EL3 ? 0 : cs->irs_idr4;
        return true;

    case A_IRS_IDR5:
        *data = cs->irs_idr5;
        return true;

    case A_IRS_IDR6:
        *data = cs->irs_idr6;
        return true;

    case A_IRS_IDR7:
        *data = cs->irs_idr7;
        return true;

    case A_IRS_IIDR:
        *data = cs->irs_iidr;
        return true;

    case A_IRS_AIDR:
        *data = cs->irs_aidr;
        return true;

    case A_IRS_IST_BASER:
        *data = extract64(cs->irs_ist_baser[domain], 0, 32);
        return true;

    case A_IRS_IST_BASER + 4:
        *data = extract64(cs->irs_ist_baser[domain], 32, 32);
        return true;

    case A_IRS_IST_STATUSR:
        /*
         * For QEMU writes to IRS_IST_BASER and IRS_MAP_L2_ISTR take effect
         * instantaneously, and the guest can never see the IDLE bit as 0.
         */
        *data = R_IRS_IST_STATUSR_IDLE_MASK;
        return true;

    case A_IRS_IST_CFGR:
        *data = cs->irs_ist_cfgr[domain];
        return true;
    }
    return false;
}

static bool config_writel(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    switch (offset) {
    case A_IRS_IST_BASER:
        irs_ist_baser_write(s, domain,
                            deposit64(cs->irs_ist_baser[domain], 0, 32, data));
        return true;
    case A_IRS_IST_BASER + 4:
        irs_ist_baser_write(s, domain,
                            deposit64(cs->irs_ist_baser[domain], 32, 32, data));
        return true;
    case A_IRS_IST_CFGR:
        if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "guest tried to write IRS_IST_CFGR for %s config frame "
                          "while IST_BASER.VALID set\n", domain_name[domain]);
        } else {
            cs->irs_ist_cfgr[domain] = data;
        }
        return true;
    }
    return false;
}

static bool config_readll(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t *data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    switch (offset) {
    case A_IRS_IST_BASER:
        *data = cs->irs_ist_baser[domain];
        return true;
    }
    return false;
}

static bool config_writell(GICv5 *s, GICv5Domain domain, hwaddr offset,
                           uint64_t data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_IRS_IST_BASER:
        irs_ist_baser_write(s, domain, data);
        return true;
    }
    return false;
}

static MemTxResult config_read(void *opaque, GICv5Domain domain, hwaddr offset,
                               uint64_t *data, unsigned size,
                               MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_readl(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_readll(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badread(domain_name[domain], offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so we log the error but return MEMTX_OK so we don't cause
         * a spurious data abort.
         */
        *data = 0;
    } else {
        trace_gicv5_read(domain_name[domain], offset, *data, size);
    }

    return MEMTX_OK;
}

static MemTxResult config_write(void *opaque, GICv5Domain domain,
                                hwaddr offset, uint64_t data, unsigned size,
                                MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_writel(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_writell(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badwrite(domain_name[domain], offset, data, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so we log the error but return MEMTX_OK so we don't cause
         * a spurious data abort.
         */
    } else {
        trace_gicv5_write(domain_name[domain], offset, data, size);
    }

    return MEMTX_OK;
}

#define DEFINE_READ_WRITE_WRAPPERS(NAME, DOMAIN)                           \
    static MemTxResult config_##NAME##_read(void *opaque, hwaddr offset,   \
                                            uint64_t *data, unsigned size, \
                                            MemTxAttrs attrs)              \
    {                                                                      \
        return config_read(opaque, DOMAIN, offset, data, size, attrs);     \
    }                                                                      \
    static MemTxResult config_##NAME##_write(void *opaque, hwaddr offset,  \
                                             uint64_t data, unsigned size, \
                                             MemTxAttrs attrs)             \
    {                                                                      \
        return config_write(opaque, DOMAIN, offset, data, size, attrs);    \
    }

DEFINE_READ_WRITE_WRAPPERS(ns, GICV5_ID_NS)
DEFINE_READ_WRITE_WRAPPERS(realm, GICV5_ID_REALM)
DEFINE_READ_WRITE_WRAPPERS(secure, GICV5_ID_S)
DEFINE_READ_WRITE_WRAPPERS(el3, GICV5_ID_EL3)

static const MemoryRegionOps config_frame_ops[NUM_GICV5_DOMAINS] = {
    [GICV5_ID_S] = {
        .read_with_attrs = config_secure_read,
        .write_with_attrs = config_secure_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_NS] = {
        .read_with_attrs = config_ns_read,
        .write_with_attrs = config_ns_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_EL3] = {
        .read_with_attrs = config_el3_read,
        .write_with_attrs = config_el3_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_REALM] = {
        .read_with_attrs = config_realm_read,
        .write_with_attrs = config_realm_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
};

static void gicv5_set_spi(void *opaque, int irq, int level)
{
    /* These irqs are all SPIs; the INTID is irq + s->spi_base */
    GICv5Common *cs = ARM_GICV5_COMMON(opaque);
    uint32_t spi_id = irq + cs->spi_base;

    trace_gicv5_spi(spi_id, level);
}

static void gicv5_reset_hold(Object *obj, ResetType type)
{
    GICv5 *s = ARM_GICV5(obj);
    GICv5Class *c = ARM_GICV5_GET_CLASS(s);

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }

    /* IRS_IST_BASER and IRS_IST_CFGR reset to 0, clear cached info */
    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        s->phys_lpi_config[i].valid = false;
    }
}

static void gicv5_set_idregs(GICv5Common *cs)
{
    /* Set the ID register value fields */
    uint32_t v;

    /*
     * We don't support any of the optional parts of the spec currently,
     * so most of the fields in IRS_IDR0 are zero.
     */
    v = 0;
    /*
     * We can handle physical addresses of any size, so report
     * support for 56 bits of physical address space.
     */
    v = FIELD_DP32(v, IRS_IDR0, PA_RANGE, 7);
    v = FIELD_DP32(v, IRS_IDR0, IRSID, cs->irsid);
    cs->irs_idr0 = v;

    v = 0;
    v = FIELD_DP32(v, IRS_IDR1, PE_CNT, cs->num_cpus);
    v = FIELD_DP32(v, IRS_IDR1, IAFFID_BITS, QEMU_GICV5_IAFFID_BITS - 1);
    v = FIELD_DP32(v, IRS_IDR1, PRI_BITS, QEMU_GICV5_PRI_BITS - 1);
    cs->irs_idr1 = v;

    v = 0;
    /* We always support physical LPIs with 2-level ISTs of all sizes */
    v = FIELD_DP32(v, IRS_IDR2, ID_BITS, QEMU_GICV5_ID_BITS);
    v = FIELD_DP32(v, IRS_IDR2, LPI, 1);
    v = FIELD_DP32(v, IRS_IDR2, MIN_LPI_ID_BITS, QEMU_GICV5_MIN_LPI_ID_BITS);
    v = FIELD_DP32(v, IRS_IDR2, IST_LEVELS, 1);
    v = FIELD_DP32(v, IRS_IDR2, IST_L2SZ, 7);
    /* Our impl does not need IST metadata, so ISTMD and ISTMD_SZ are 0 */
    cs->irs_idr2 = v;

    /* We don't implement virtualization yet, so these are zero */
    cs->irs_idr3 = 0;
    cs->irs_idr4 = 0;

    /* These three have just one field each */
    cs->irs_idr5 = FIELD_DP32(0, IRS_IDR5, SPI_RANGE, cs->spi_range);
    cs->irs_idr6 = FIELD_DP32(0, IRS_IDR6, SPI_IRS_RANGE, cs->spi_irs_range);
    cs->irs_idr7 = FIELD_DP32(0, IRS_IDR7, SPI_BASE, cs->spi_base);

    v = 0;
    v = FIELD_DP32(v, IRS_IIDR, IMPLEMENTER, QEMU_GICV5_IMPLEMENTER);
    v = FIELD_DP32(v, IRS_IIDR, REVISION, QEMU_GICV5_REVISION);
    v = FIELD_DP32(v, IRS_IIDR, VARIANT, QEMU_GICV5_VARIANT);
    v = FIELD_DP32(v, IRS_IIDR, PRODUCTID, QEMU_GICV5_PRODUCTID);
    cs->irs_iidr = v;

    /* This is a GICv5.0 IRS, so all fields are zero */
    cs->irs_aidr = 0;
}

static void gicv5_realize(DeviceState *dev, Error **errp)
{
    GICv5Common *cs = ARM_GICV5_COMMON(dev);
    GICv5Class *gc = ARM_GICV5_GET_CLASS(dev);
    Error *migration_blocker = NULL;

    ERRP_GUARD();

    gc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    error_setg(&migration_blocker,
               "Live migration disabled: not yet supported by GICv5");
    if (migrate_add_blocker(&migration_blocker, errp)) {
        return;
    }

    /*
     * When we implement support for more than one interrupt domain,
     * we will provide some QOM properties so the board can configure
     * which domains are implemented. For now, we only implement the
     * NS domain.
     */
    cs->implemented_domains = (1 << GICV5_ID_NS);

    gicv5_set_idregs(cs);
    gicv5_common_init_irqs_and_mmio(cs, gicv5_set_spi, config_frame_ops);
}

static void gicv5_init(Object *obj)
{
}

static void gicv5_finalize(Object *obj)
{
}

static void gicv5_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    GICv5Class *gc = ARM_GICV5_CLASS(oc);

    device_class_set_parent_realize(dc, gicv5_realize, &gc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, gicv5_reset_hold, NULL,
                                       &gc->parent_phases);
}
