/*
 * ITS emulation for a GICv3-based system
 *
 * Copyright Linaro.org 2021
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "gicv3_internal.h"
#include "qom/object.h"

typedef struct GICv3ITSClass GICv3ITSClass;
/* This is reusing the GICv3ITSState typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3ITSState, GICv3ITSClass,
                     ARM_GICV3_ITS, TYPE_ARM_GICV3_ITS)

struct GICv3ITSClass {
    GICv3ITSCommonClass parent_class;
    void (*parent_reset)(DeviceState *dev);
};

static MemTxResult process_sync(GICv3ITSState *s, uint32_t offset)
{
    AddressSpace *as = &s->gicv3->sysmem_as;
    uint64_t rdbase;
    uint64_t value;
    bool pta = false;
    MemTxResult res = MEMTX_OK;

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                     MEMTXATTRS_UNSPECIFIED, &res);

    if (FIELD_EX64(s->typer, GITS_TYPER, PTA)) {
        /*
         * only bits[47:16] are considered instead of bits [51:16]
         * since with a physical address the target address must be
         * 64KB aligned
         */
        rdbase = (value >> RDBASE_OFFSET) & RDBASE_MASK;
        pta = true;
    } else {
        rdbase = (value >> RDBASE_OFFSET) & RDBASE_PROCNUM_MASK;
    }

    if (!pta && (rdbase < (s->gicv3->num_cpu))) {
        /*
         * Current implementation makes a blocking synchronous call
         * for every command issued earlier,hence the internal state
         * is already consistent by the time SYNC command is executed.
         */
    }

    offset += NUM_BYTES_IN_DW;
    return res;
}

static void update_cte(GICv3ITSState *s, uint16_t icid, uint64_t cte)
{
    AddressSpace *as = &s->gicv3->sysmem_as;
    uint64_t value;
    uint8_t  page_sz_type;
    uint64_t l2t_addr;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t page_sz = 0;
    uint32_t max_l2_entries;

    if (s->ct.indirect) {
        /* 2 level table */
        page_sz_type = FIELD_EX64(s->baser[1], GITS_BASER, PAGESIZE);

        if (page_sz_type == 0) {
            page_sz = GITS_ITT_PAGE_SIZE_0;
        } else if (page_sz_type == 1) {
            page_sz = GITS_ITT_PAGE_SIZE_1;
        } else if (page_sz_type == 2) {
            page_sz = GITS_ITT_PAGE_SIZE_2;
        }

        l2t_id = icid / (page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->ct.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, NULL);

        valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

        if (valid_l2t) {
            max_l2_entries = page_sz / s->ct.entry_sz;

            l2t_addr = (value >> page_sz_type) &
                        ((1ULL << (51 - page_sz_type)) - 1);

            address_space_write(as, l2t_addr +
                                 ((icid % max_l2_entries) * GITS_CTE_SIZE),
                                 MEMTXATTRS_UNSPECIFIED,
                                 &cte, sizeof(cte));
        }
    } else {
        /* Flat level table */
        address_space_write(as, s->ct.base_addr + (icid * GITS_CTE_SIZE),
                            MEMTXATTRS_UNSPECIFIED, &cte,
                            sizeof(cte));
    }
}

static MemTxResult process_mapc(GICv3ITSState *s, uint32_t offset)
{
    AddressSpace *as = &s->gicv3->sysmem_as;
    uint16_t icid;
    uint64_t rdbase;
    bool valid;
    bool pta = false;
    MemTxResult res = MEMTX_OK;
    uint64_t cte_entry;
    uint64_t value;

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    icid = value & ICID_MASK;

    if (FIELD_EX64(s->typer, GITS_TYPER, PTA)) {
        /*
         * only bits[47:16] are considered instead of bits [51:16]
         * since with a physical address the target address must be
         * 64KB aligned
         */
        rdbase = (value >> RDBASE_OFFSET) & RDBASE_MASK;
        pta = true;
    } else {
        rdbase = (value >> RDBASE_OFFSET) & RDBASE_PROCNUM_MASK;
    }

    valid = (value >> VALID_SHIFT) & VALID_MASK;

    if (valid) {
        if ((icid > s->ct.max_collids) || (!pta &&
                (rdbase > s->gicv3->num_cpu))) {
            if (FIELD_EX64(s->typer, GITS_TYPER, SEIS)) {
                /* Generate System Error here if supported */
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: invalid collection table attributes "
                "icid %d rdbase %lu\n", __func__, icid, rdbase);
            /*
             * in this implementation,in case of error
             * we ignore this command and move onto the next
             * command in the queue
             */
        } else {
            if (s->ct.valid) {
                /* add mapping entry to collection table */
                cte_entry = (valid & VALID_MASK) |
                            (pta ? ((rdbase & RDBASE_MASK) << 1ULL) :
                            ((rdbase & RDBASE_PROCNUM_MASK) << 1ULL));

                update_cte(s, icid, cte_entry);
            }
        }
    } else {
        if (s->ct.valid) {
            /* remove mapping entry from collection table */
            cte_entry = 0;

            update_cte(s, icid, cte_entry);
        }
    }

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    return res;
}

static void update_dte(GICv3ITSState *s, uint32_t devid, uint64_t dte)
{
    AddressSpace *as = &s->gicv3->sysmem_as;
    uint64_t value;
    uint8_t  page_sz_type;
    uint64_t l2t_addr;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t page_sz = 0;
    uint32_t max_l2_entries;

    if (s->dt.indirect) {
        /* 2 level table */
        page_sz_type = FIELD_EX64(s->baser[0], GITS_BASER, PAGESIZE);

        if (page_sz_type == 0) {
            page_sz = GITS_ITT_PAGE_SIZE_0;
        } else if (page_sz_type == 1) {
            page_sz = GITS_ITT_PAGE_SIZE_1;
        } else if (page_sz_type == 2) {
            page_sz = GITS_ITT_PAGE_SIZE_2;
        }

        l2t_id = devid / (page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->dt.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, NULL);

        valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

        if (valid_l2t) {
            max_l2_entries = page_sz / s->dt.entry_sz;

            l2t_addr = (value >> page_sz_type) &
                        ((1ULL << (51 - page_sz_type)) - 1);

            address_space_write(as, l2t_addr +
                                 ((devid % max_l2_entries) * GITS_DTE_SIZE),
                                 MEMTXATTRS_UNSPECIFIED, &dte, sizeof(dte));
        }
    } else {
        /* Flat level table */
        address_space_write(as, s->dt.base_addr + (devid * GITS_DTE_SIZE),
                            MEMTXATTRS_UNSPECIFIED, &dte, sizeof(dte));
    }
}

static MemTxResult process_mapd(GICv3ITSState *s, uint64_t value,
                                 uint32_t offset)
{
    AddressSpace *as = &s->gicv3->sysmem_as;
    uint32_t devid;
    uint8_t size;
    uint64_t itt_addr;
    bool valid;
    MemTxResult res = MEMTX_OK;
    uint64_t dte_entry = 0;

    devid = (value >> DEVID_OFFSET) & DEVID_MASK;

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);
    size = (value & SIZE_MASK);

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);
    itt_addr = (value >> ITTADDR_OFFSET) & ITTADDR_MASK;

    valid = (value >> VALID_SHIFT) & VALID_MASK;

    if (valid) {
        if ((devid > s->dt.max_devids) ||
            (size > FIELD_EX64(s->typer, GITS_TYPER, IDBITS))) {
            if (FIELD_EX64(s->typer, GITS_TYPER, SEIS)) {
                /* Generate System Error here if supported */
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: invalid device table attributes "
                "devid %d or size %d\n", __func__, devid, size);
            /*
             * in this implementation,in case of error
             * we ignore this command and move onto the next
             * command in the queue
             */
        } else {
            if (s->dt.valid) {
                /* add mapping entry to device table */
                dte_entry = (valid & VALID_MASK) |
                            ((size & SIZE_MASK) << 1U) |
                            ((itt_addr & ITTADDR_MASK) << 6ULL);

                update_dte(s, devid, dte_entry);
            }
        }
    } else {
        if (s->dt.valid) {
            /* remove mapping entry from device table */
            dte_entry = 0;
            update_dte(s, devid, dte_entry);
        }
    }

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    return res;
}

/*
 * Current implementation blocks until all
 * commands are processed
 */
static MemTxResult process_cmdq(GICv3ITSState *s)
{
    uint32_t wr_offset = 0;
    uint32_t rd_offset = 0;
    uint32_t cq_offset = 0;
    uint64_t data;
    AddressSpace *as = &s->gicv3->sysmem_as;
    MemTxResult res = MEMTX_OK;
    uint8_t cmd;

    wr_offset = FIELD_EX64(s->cwriter, GITS_CWRITER, OFFSET);

    if (wr_offset > s->cq.max_entries) {
        qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: invalid write offset "
                        "%d\n", __func__, wr_offset);
        res = MEMTX_ERROR;
        return res;
    }

    rd_offset = FIELD_EX64(s->creadr, GITS_CREADR, OFFSET);

    while (wr_offset != rd_offset) {
        cq_offset = (rd_offset * GITS_CMDQ_ENTRY_SIZE);
        data = address_space_ldq_le(as, s->cq.base_addr + cq_offset,
                                      MEMTXATTRS_UNSPECIFIED, &res);
        cmd = (data & CMD_MASK);

        switch (cmd) {
        case GITS_CMD_INT:
            break;
        case GITS_CMD_CLEAR:
            break;
        case GITS_CMD_SYNC:
            res = process_sync(s, cq_offset);
            break;
        case GITS_CMD_MAPD:
            res = process_mapd(s, data, cq_offset);
            break;
        case GITS_CMD_MAPC:
            res = process_mapc(s, cq_offset);
            break;
        case GITS_CMD_MAPTI:
            break;
        case GITS_CMD_MAPI:
            break;
        case GITS_CMD_DISCARD:
            break;
        default:
            break;
        }
        if (res == MEMTX_OK) {
            rd_offset++;
            rd_offset %= s->cq.max_entries;
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, OFFSET, rd_offset);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: %x cmd processing failed!!\n", __func__, cmd);
            break;
        }
    }
    return res;
}

static bool extract_table_params(GICv3ITSState *s, int index)
{
    uint16_t num_pages = 0;
    uint8_t  page_sz_type;
    uint8_t type;
    uint32_t page_sz = 0;
    uint64_t value = s->baser[index];

    num_pages = FIELD_EX64(value, GITS_BASER, SIZE);
    page_sz_type = FIELD_EX64(value, GITS_BASER, PAGESIZE);

    if (page_sz_type == 0) {
        page_sz = GITS_ITT_PAGE_SIZE_0;
    } else if (page_sz_type == 0) {
        page_sz = GITS_ITT_PAGE_SIZE_1;
    } else if (page_sz_type == 2) {
        page_sz = GITS_ITT_PAGE_SIZE_2;
    } else {
        return false;
    }

    type = FIELD_EX64(value, GITS_BASER, TYPE);

    if (type == GITS_ITT_TYPE_DEVICE) {
        s->dt.valid = FIELD_EX64(value, GITS_BASER, VALID);

        if (s->dt.valid) {
            s->dt.indirect = FIELD_EX64(value, GITS_BASER, INDIRECT);
            s->dt.entry_sz = FIELD_EX64(value, GITS_BASER, ENTRYSIZE);

            if (!s->dt.indirect) {
                s->dt.max_entries = ((num_pages + 1) * page_sz) /
                                                       s->dt.entry_sz;
            } else {
                s->dt.max_entries = ((((num_pages + 1) * page_sz) /
                                        L1TABLE_ENTRY_SIZE) *
                                    (page_sz / s->dt.entry_sz));
            }

            s->dt.max_devids = (1UL << (FIELD_EX64(s->typer, GITS_TYPER,
                                                    DEVBITS) + 1));

            if ((page_sz == GITS_ITT_PAGE_SIZE_0) ||
                    (page_sz == GITS_ITT_PAGE_SIZE_1)) {
                s->dt.base_addr = FIELD_EX64(value, GITS_BASER, PHYADDR);
                s->dt.base_addr <<= R_GITS_BASER_PHYADDR_SHIFT;
            } else if (page_sz == GITS_ITT_PAGE_SIZE_2) {
                s->dt.base_addr = FIELD_EX64(value, GITS_BASER, PHYADDRL_64K) <<
                                  R_GITS_BASER_PHYADDRL_64K_SHIFT;
                  s->dt.base_addr |= ((value >> R_GITS_BASER_PHYADDR_SHIFT) &
                                       R_GITS_BASER_PHYADDRH_64K_MASK) <<
                                       R_GITS_BASER_PHYADDRH_64K_SHIFT;
            }
        }
    } else if (type == GITS_ITT_TYPE_COLLECTION) {
        s->ct.valid = FIELD_EX64(value, GITS_BASER, VALID);

        /*
         * GITS_TYPER.HCC is 0 for this implementation
         * hence writes are discarded if ct.valid is 0
         */
        if (s->ct.valid) {
            s->ct.indirect = FIELD_EX64(value, GITS_BASER, INDIRECT);
            s->ct.entry_sz = FIELD_EX64(value, GITS_BASER, ENTRYSIZE);

            if (!s->ct.indirect) {
                s->ct.max_entries = ((num_pages + 1) * page_sz) /
                                      s->ct.entry_sz;
            } else {
                s->ct.max_entries = ((((num_pages + 1) * page_sz) /
                                      L1TABLE_ENTRY_SIZE) *
                                      (page_sz / s->ct.entry_sz));
            }

            if (FIELD_EX64(s->typer, GITS_TYPER, CIL)) {
                s->ct.max_collids = (1UL << (FIELD_EX64(s->typer, GITS_TYPER,
                                                         CIDBITS) + 1));
            } else {
                /* 16-bit CollectionId supported when CIL == 0 */
                s->ct.max_collids = (1UL << 16);
            }

            if ((page_sz == GITS_ITT_PAGE_SIZE_0) ||
                 (page_sz == GITS_ITT_PAGE_SIZE_1)) {
                s->ct.base_addr = FIELD_EX64(value, GITS_BASER, PHYADDR);
                s->ct.base_addr <<= R_GITS_BASER_PHYADDR_SHIFT;
            } else if (page_sz == GITS_ITT_PAGE_SIZE_2) {
                s->ct.base_addr = FIELD_EX64(value, GITS_BASER, PHYADDRL_64K) <<
                                    R_GITS_BASER_PHYADDRL_64K_SHIFT;
                s->ct.base_addr |= ((value >> R_GITS_BASER_PHYADDR_SHIFT) &
                                     R_GITS_BASER_PHYADDRH_64K_MASK) <<
                                     R_GITS_BASER_PHYADDRH_64K_SHIFT;
            }
        }
    } else {
        /* unsupported ITS table type */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported ITS table type %d",
                         __func__, type);
        return false;
    }
    return true;
}

static bool extract_cmdq_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint64_t value = s->cbaser;

    num_pages = FIELD_EX64(value, GITS_CBASER, SIZE);

    s->cq.valid = FIELD_EX64(value, GITS_CBASER, VALID);

    if (!num_pages || !s->cq.valid) {
        return false;
    }

    if (s->cq.valid) {
        s->cq.max_entries = ((num_pages + 1) * GITS_ITT_PAGE_SIZE_0) /
                                                GITS_CMDQ_ENTRY_SIZE;
        s->cq.base_addr = FIELD_EX64(value, GITS_CBASER, PHYADDR);
        s->cq.base_addr <<= R_GITS_CBASER_PHYADDR_SHIFT;
    }
    return true;
}

static MemTxResult its_trans_writew(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;

    return result;
}

static MemTxResult its_trans_writel(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;

    return result;
}

static MemTxResult gicv3_its_translation_write(void *opaque, hwaddr offset,
                               uint64_t data, unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    MemTxResult result;

    switch (size) {
    case 2:
        result = its_trans_writew(s, offset, data, attrs);
        break;
    case 4:
        result = its_trans_writel(s, offset, data, attrs);
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }

    if (result == MEMTX_ERROR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        result = MEMTX_OK;
    }
    return result;
}

static MemTxResult gicv3_its_translation_read(void *opaque, hwaddr offset,
                              uint64_t *data, unsigned size, MemTxAttrs attrs)
{
    qemu_log_mask(LOG_GUEST_ERROR,
        "%s: Invalid read from translation register area at offset "
        TARGET_FMT_plx "\n", __func__, offset);
    return MEMTX_ERROR;
}

static MemTxResult its_writeb(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP,
                "%s: unsupported byte write to register at offset "
                TARGET_FMT_plx "\n", __func__, offset);
    return MEMTX_ERROR;
}

static MemTxResult its_readb(GICv3ITSState *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP,
                "%s: unsupported byte read from register at offset "
                TARGET_FMT_plx "\n", __func__, offset);
    return MEMTX_ERROR;
}

static MemTxResult its_writew(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP,
        "%s: unsupported word write to register at offset "
        TARGET_FMT_plx "\n", __func__, offset);
    return MEMTX_ERROR;
}

static MemTxResult its_readw(GICv3ITSState *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP,
        "%s: unsupported word read from register at offset "
        TARGET_FMT_plx "\n", __func__, offset);
    return MEMTX_ERROR;
}

static MemTxResult its_writel(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    int index;
    uint64_t temp = 0;

    switch (offset) {
    case GITS_CTLR:
        s->ctlr |= (value & ~(s->ctlr));
        break;
    case GITS_CBASER:
        /* GITS_CBASER register becomes RO if ITS is already enabled */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 0, 32, value);
            s->creadr = 0;
        }
        break;
    case GITS_CBASER + 4:
        /* GITS_CBASER register becomes RO if ITS is already enabled */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 32, 32, value);
            if (!extract_cmdq_params(s)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                       "%s: error extracting GITS_CBASER parameters "
                       TARGET_FMT_plx "\n", __func__, offset);
                s->cbaser = 0;
                result = MEMTX_ERROR;
            } else {
                s->creadr = 0;
            }
        }
        break;
    case GITS_CWRITER:
        s->cwriter = deposit64(s->cwriter, 0, 32, value);
        if ((s->ctlr & ITS_CTLR_ENABLED) && (s->cwriter != s->creadr)) {
            result = process_cmdq(s);
        }
        break;
    case GITS_CWRITER + 4:
        s->cwriter = deposit64(s->cwriter, 32, 32, value);
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        /* GITS_BASERn registers become RO if ITS is already enabled */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;

            if (offset & 7) {
                temp = s->baser[index];
                temp = deposit64(temp, 32, 32, (value & ~GITS_BASER_VAL_MASK));
                s->baser[index] |= temp;

                if (!extract_table_params(s, index)) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: error extracting GITS_BASER parameters "
                        TARGET_FMT_plx "\n", __func__, offset);
                    s->baser[index] = 0;
                    result = MEMTX_ERROR;
                }
            } else {
                s->baser[index] =  deposit64(s->baser[index], 0, 32, value);
            }
        }
        break;
    case GITS_IIDR:
    case GITS_TYPER:
    case GITS_CREADR:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: invalid guest write to RO register at offset "
            TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }
    return result;
}

static MemTxResult its_readl(GICv3ITSState *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    int index;

    switch (offset) {
    case GITS_CTLR:
        *data = s->ctlr;
        break;
    case GITS_IIDR:
        *data = s->iidr;
        break;
    case GITS_PIDR2:
        *data = 0x30; /* GICv3 */
        break;
    case GITS_TYPER:
        *data = extract64(s->typer, 0, 32);
        break;
    case GITS_TYPER + 4:
        *data = extract64(s->typer, 32, 32);
        break;
    case GITS_CBASER:
        *data = extract64(s->cbaser, 0, 32);
        break;
    case GITS_CBASER + 4:
        *data = extract64(s->cbaser, 32, 32);
        break;
    case GITS_CREADR:
        *data = extract64(s->creadr, 0, 32);
        break;
    case GITS_CREADR + 4:
        *data = extract64(s->creadr, 32, 32);
        break;
    case GITS_CWRITER:
        *data = extract64(s->cwriter, 0, 32);
        break;
    case GITS_CWRITER + 4:
        *data = extract64(s->cwriter, 32, 32);
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        if (offset & 7) {
            *data = s->baser[index] >> 32;
        } else {
            *data = (uint32_t)s->baser[index];
        }
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }
    return result;
}

static MemTxResult its_writell(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    int index;

    switch (offset) {
    case GITS_BASER ... GITS_BASER + 0x3f:
        /* GITS_BASERn registers become RO if ITS is already enabled */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;
            s->baser[index] |= (value & ~GITS_BASER_VAL_MASK);
            if (!extract_table_params(s, index)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: error extracting GITS_BASER parameters "
                        TARGET_FMT_plx "\n", __func__, offset);
                s->baser[index] = 0;
                result = MEMTX_ERROR;
            }
        }
        break;
    case GITS_CBASER:
        /* GITS_CBASER register becomes RO if ITS is already enabled */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = value;
            if (!extract_cmdq_params(s)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                       "%s: error extracting GITS_CBASER parameters "
                       TARGET_FMT_plx "\n", __func__, offset);
                s->cbaser = 0;
                result = MEMTX_ERROR;
            } else {
                s->creadr = 0;
            }
        }
        break;
    case GITS_CWRITER:
        s->cwriter = value;
        if ((s->ctlr & ITS_CTLR_ENABLED) && (s->cwriter != s->creadr)) {
            result = process_cmdq(s);
        }
        break;
    case GITS_TYPER:
    case GITS_CREADR:
        /* RO register, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }
    return result;
}

static MemTxResult its_readll(GICv3ITSState *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    MemTxResult result = MEMTX_OK;
    int index;

    switch (offset) {
    case GITS_TYPER:
        *data = s->typer;
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        *data = s->baser[index];
        break;
    case GITS_CBASER:
        *data = s->cbaser;
        break;
    case GITS_CREADR:
        *data = s->creadr;
        break;
    case GITS_CWRITER:
        *data = s->cwriter;
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }
    return result;
}

static MemTxResult gicv3_its_read(void *opaque, hwaddr offset, uint64_t *data,
                              unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    MemTxResult result;

    switch (size) {
    case 1:
        result = its_readb(s, offset, data, attrs);
        break;
    case 2:
        result = its_readw(s, offset, data, attrs);
        break;
    case 4:
        result = its_readl(s, offset, data, attrs);
        break;
    case 8:
        result = its_readll(s, offset, data, attrs);
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }

    if (result == MEMTX_ERROR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        result = MEMTX_OK;
        *data = 0;
    }
    return result;
}

static MemTxResult gicv3_its_write(void *opaque, hwaddr offset, uint64_t data,
                               unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    MemTxResult result;

    switch (size) {
    case 1:
        result = its_writeb(s, offset, data, attrs);
        break;
    case 2:
        result = its_writew(s, offset, data, attrs);
        break;
    case 4:
        result = its_writel(s, offset, data, attrs);
        break;
    case 8:
        result = its_writell(s, offset, data, attrs);
        break;
    default:
        result = MEMTX_ERROR;
        break;
    }

    if (result == MEMTX_ERROR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        result = MEMTX_OK;
    }
    return result;
}

static const MemoryRegionOps gicv3_its_control_ops = {
    .read_with_attrs = gicv3_its_read,
    .write_with_attrs = gicv3_its_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gicv3_its_translation_ops = {
    .read_with_attrs = gicv3_its_translation_read,
    .write_with_attrs = gicv3_its_translation_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gicv3_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);

    gicv3_its_init_mmio(s, &gicv3_its_control_ops, &gicv3_its_translation_ops);

    address_space_init(&s->gicv3->sysmem_as, s->gicv3->sysmem,
                        "gicv3-its-sysmem");
}

static void gicv3_its_reset(DeviceState *dev)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    GICv3ITSClass *c = ARM_GICV3_ITS_GET_CLASS(s);

    if (s->gicv3->cpu->gicr_typer & GICR_TYPER_PLPIS) {
        c->parent_reset(dev);
        memset(&s->dt, 0, sizeof(s->dt));
        memset(&s->ct, 0, sizeof(s->ct));
        memset(&s->cq, 0, sizeof(s->cq));

        /* set the ITS default features supported */
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, PHYSICAL,
                                       GITS_TYPE_PHYSICAL);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, ITT_ENTRY_SIZE,
                                       ITS_ITT_ENTRY_SIZE);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, IDBITS, ITS_IDBITS);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, DEVBITS, ITS_DEVBITS);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIL, 1);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIDBITS, ITS_CIDBITS);

        /*
         * We claim to be an ARM r0p0 with a zero ProductID.
         * This is the same as an r0p0 GIC-500.
         */
        s->iidr = gicv3_iidr();

        /* Quiescent bit reset to 1 */
        s->ctlr = FIELD_DP32(s->ctlr, GITS_CTLR, QUIESCENT, 1);

        /*
         * setting GITS_BASER0.Type = 0b001 (Device)
         *         GITS_BASER1.Type = 0b100 (Collection Table)
         *         GITS_BASER<n>.Type,where n = 3 to 7 are 0b00 (Unimplemented)
         *         GITS_BASER<0,1>.Page_Size = 64KB
         * and default translation table entry size to 16 bytes
         */
        s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, TYPE,
                                              GITS_ITT_TYPE_DEVICE);
        s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, PAGESIZE,
                                              GITS_BASER_PAGESIZE_64K);
        s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, ENTRYSIZE,
                                              GITS_DTE_SIZE);

        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, TYPE,
                                              GITS_ITT_TYPE_COLLECTION);
        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, PAGESIZE,
                                              GITS_BASER_PAGESIZE_64K);
        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, ENTRYSIZE,
                                              GITS_CTE_SIZE);
    }
}

static Property gicv3_its_props[] = {
    DEFINE_PROP_LINK("parent-gicv3", GICv3ITSState, gicv3, "arm-gicv3",
                     GICv3State *),
    DEFINE_PROP_END_OF_LIST(),
};

static void gicv3_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    GICv3ITSClass *ic = ARM_GICV3_ITS_CLASS(klass);

    dc->realize = gicv3_arm_its_realize;
    device_class_set_props(dc, gicv3_its_props);
    device_class_set_parent_reset(dc, gicv3_its_reset, &ic->parent_reset);
}

static const TypeInfo gicv3_its_info = {
    .name = TYPE_ARM_GICV3_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .class_init = gicv3_its_class_init,
    .class_size = sizeof(GICv3ITSClass),
};

static void gicv3_its_register_types(void)
{
    type_register_static(&gicv3_its_info);
}

type_init(gicv3_its_register_types)
