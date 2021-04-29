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

typedef enum ItsCmdType {
    NONE = 0, /* internal indication for GITS_TRANSLATER write */
    CLEAR = 1,
    DISCARD = 2,
    INT = 3,
} ItsCmdType;

static bool get_cte(GICv3ITSState *s, uint16_t icid, uint64_t *cte,
    MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t l2t_addr;
    uint64_t value;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t max_l2_entries;
    bool status = false;

    if (s->ct.indirect) {
        l2t_id = icid / (s->ct.page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->ct.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, res);

        if (*res == MEMTX_OK) {
            valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

            if (valid_l2t) {
                max_l2_entries = s->ct.page_sz / s->ct.entry_sz;

                l2t_addr = value & ((1ULL << 51) - 1);

                *cte =  address_space_ldq_le(as, l2t_addr +
                                    ((icid % max_l2_entries) * GITS_CTE_SIZE),
                                    MEMTXATTRS_UNSPECIFIED, res);
           }
       }
    } else {
        /* Flat level table */
        *cte =  address_space_ldq_le(as, s->ct.base_addr +
                                     (icid * GITS_CTE_SIZE),
                                      MEMTXATTRS_UNSPECIFIED, res);
    }

    if (*cte & VALID_MASK) {
        status = true;
    }

    return status;
}

static MemTxResult update_ite(GICv3ITSState *s, uint32_t eventid, uint64_t dte,
    uint64_t itel, uint32_t iteh)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t itt_addr;
    MemTxResult res = MEMTX_OK;

    itt_addr = (dte >> 6ULL) & ITTADDR_MASK;
    itt_addr <<= ITTADDR_SHIFT; /* 256 byte aligned */

    address_space_stq_le(as, itt_addr + (eventid * sizeof(uint64_t)),
                         itel, MEMTXATTRS_UNSPECIFIED, &res);

    if (res == MEMTX_OK) {
        address_space_stl_le(as, itt_addr + ((eventid + sizeof(uint64_t)) *
                             sizeof(uint32_t)), iteh, MEMTXATTRS_UNSPECIFIED,
                             &res);
    }
   return res;
}

static bool get_ite(GICv3ITSState *s, uint32_t eventid, uint64_t dte,
                      uint16_t *icid, uint32_t *pIntid, MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t itt_addr;
    bool status = false;
    uint64_t itel = 0;
    uint32_t iteh = 0;

    itt_addr = (dte >> 6ULL) & ITTADDR_MASK;
    itt_addr <<= ITTADDR_SHIFT; /* 256 byte aligned */

    itel = address_space_ldq_le(as, itt_addr + (eventid * sizeof(uint64_t)),
                                MEMTXATTRS_UNSPECIFIED, res);

    if (*res == MEMTX_OK) {
        iteh = address_space_ldl_le(as, itt_addr + ((eventid +
                                    sizeof(uint64_t)) * sizeof(uint32_t)),
                                    MEMTXATTRS_UNSPECIFIED, res);

        if (*res == MEMTX_OK) {
            if (itel & VALID_MASK) {
                if ((itel >> ITE_ENTRY_INTTYPE_SHIFT) & GITS_TYPE_PHYSICAL) {
                    *pIntid = (itel >> ITE_ENTRY_INTID_SHIFT) &
                              ITE_ENTRY_INTID_MASK;
                    *icid = iteh & ITE_ENTRY_ICID_MASK;
                    status = true;
                }
            }
        }
    }
    return status;
}

static uint64_t get_dte(GICv3ITSState *s, uint32_t devid, MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t l2t_addr;
    uint64_t value;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t max_l2_entries;

    if (s->dt.indirect) {
        l2t_id = devid / (s->dt.page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->dt.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, res);

        if (*res == MEMTX_OK) {
            valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

            if (valid_l2t) {
                max_l2_entries = s->dt.page_sz / s->dt.entry_sz;

                l2t_addr = value & ((1ULL << 51) - 1);

                value = 0;
                value =  address_space_ldq_le(as, l2t_addr +
                                   ((devid % max_l2_entries) * GITS_DTE_SIZE),
                                   MEMTXATTRS_UNSPECIFIED, res);
            }
        }
    } else {
        /* Flat level table */
        value = 0;
        value = address_space_ldq_le(as, s->dt.base_addr +
                                           (devid * GITS_DTE_SIZE),
                                    MEMTXATTRS_UNSPECIFIED, res);
    }

    return value;
}

static MemTxResult process_sync(GICv3ITSState *s, uint32_t offset)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t rdbase;
    uint64_t value;
    MemTxResult res = MEMTX_OK;

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                     MEMTXATTRS_UNSPECIFIED, &res);

    rdbase = (value >> RDBASE_SHIFT) & RDBASE_PROCNUM_MASK;

    if (rdbase < (s->gicv3->num_cpu)) {
        /*
         * Current implementation makes a blocking synchronous call
         * for every command issued earlier,hence the internal state
         * is already consistent by the time SYNC command is executed.
         */
    }

    offset += NUM_BYTES_IN_DW;
    return res;
}

static MemTxResult process_int(GICv3ITSState *s, uint64_t value,
                                uint32_t offset, ItsCmdType cmd)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid, eventid;
    MemTxResult res = MEMTX_OK;
    bool dte_valid;
    uint64_t dte = 0;
    uint32_t max_eventid;
    uint16_t icid = 0;
    uint32_t pIntid = 0;
    bool ite_valid = false;
    uint64_t cte = 0;
    bool cte_valid = false;
    uint64_t itel = 0;
    uint32_t iteh = 0;

    if (cmd == NONE) {
        devid = offset;
    } else {
        devid = (value >> DEVID_SHIFT) & DEVID_MASK;

        offset += NUM_BYTES_IN_DW;
        value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);
    }

    if (res != MEMTX_OK) {
        return res;
    }

    eventid = (value & EVENTID_MASK);

    dte = get_dte(s, devid, &res);

    if (res != MEMTX_OK) {
        return res;
    }
    dte_valid = dte & VALID_MASK;

    if (dte_valid) {
        max_eventid = (1UL << (((dte >> 1U) & SIZE_MASK) + 1));

        ite_valid = get_ite(s, eventid, dte, &icid, &pIntid, &res);

        if (res != MEMTX_OK) {
            return res;
        }

        if (ite_valid) {
            cte_valid = get_cte(s, icid, &cte, &res);
        }

        if (res != MEMTX_OK) {
            return res;
        }
    }

    if ((devid > s->dt.max_devids) || !dte_valid || !ite_valid ||
            !cte_valid || (eventid > max_eventid)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: invalid interrupt translation table attributes "
            "devid %d or eventid %d\n",
            __func__, devid, eventid);
        /*
         * in this implementation,in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
    } else {
        /*
         * Current implementation only supports rdbase == procnum
         * Hence rdbase physical address is ignored
         */
        if (cmd == DISCARD) {
            /* remove mapping from interrupt translation table */
            res = update_ite(s, eventid, dte, itel, iteh);
        }
    }

    if (cmd != NONE) {
        offset += NUM_BYTES_IN_DW;
        offset += NUM_BYTES_IN_DW;
    }

    return res;
}

static MemTxResult process_mapti(GICv3ITSState *s, uint64_t value,
                                    uint32_t offset, bool ignore_pInt)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid, eventid;
    uint32_t pIntid = 0;
    uint32_t max_eventid, max_Intid;
    bool dte_valid;
    MemTxResult res = MEMTX_OK;
    uint16_t icid = 0;
    uint64_t dte = 0;
    uint64_t itel = 0;
    uint32_t iteh = 0;
    uint32_t int_spurious = INTID_SPURIOUS;

    devid = (value >> DEVID_SHIFT) & DEVID_MASK;
    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return res;
    }

    eventid = (value & EVENTID_MASK);

    if (!ignore_pInt) {
        pIntid = (value >> pINTID_OFFSET) & pINTID_MASK;
    }

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return res;
    }

    icid = value & ICID_MASK;

    dte = get_dte(s, devid, &res);

    if (res != MEMTX_OK) {
        return res;
    }
    dte_valid = dte & VALID_MASK;

    max_eventid = (1UL << (((dte >> 1U) & SIZE_MASK) + 1));

    if (!ignore_pInt) {
        max_Intid = (1UL << (FIELD_EX64(s->typer, GITS_TYPER, IDBITS) + 1));
    }

    if ((devid > s->dt.max_devids) || (icid > s->ct.max_collids) ||
            !dte_valid || (eventid > max_eventid) ||
            (!ignore_pInt && ((pIntid < GICV3_LPI_INTID_START) ||
               (pIntid > max_Intid)))) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: invalid interrupt translation table attributes "
            "devid %d or icid %d or eventid %d or pIntid %d\n",
            __func__, devid, icid, eventid, pIntid);
        /*
         * in this implementation,in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
    } else {
        /* add ite entry to interrupt translation table */
        itel = (dte_valid & VALID_MASK) | (GITS_TYPE_PHYSICAL <<
                                           ITE_ENTRY_INTTYPE_SHIFT);

        if (ignore_pInt) {
            itel |= (eventid << ITE_ENTRY_INTID_SHIFT);
        } else {
            itel |= (pIntid << ITE_ENTRY_INTID_SHIFT);
        }
        itel |= (int_spurious << ITE_ENTRY_INTSP_SHIFT);
        iteh |= icid;

        res = update_ite(s, eventid, dte, itel, iteh);
    }

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    return res;
}

static MemTxResult update_cte(GICv3ITSState *s, uint16_t icid, bool valid,
    uint64_t rdbase)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t value;
    uint64_t l2t_addr;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t max_l2_entries;
    uint64_t cte = 0;
    MemTxResult res = MEMTX_OK;

    if (s->ct.valid) {
        if (valid) {
            /* add mapping entry to collection table */
            cte = (valid & VALID_MASK) |
                  ((rdbase & RDBASE_PROCNUM_MASK) << 1ULL);
        }
    } else {
        return res;
    }

    /*
     * The specification defines the format of level 1 entries of a
     * 2-level table, but the format of level 2 entries and the format
     * of flat-mapped tables is IMPDEF.
     */
    if (s->ct.indirect) {
        l2t_id = icid / (s->ct.page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->ct.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, &res);

        if (res != MEMTX_OK) {
            return res;
        }

        valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

        if (valid_l2t) {
            max_l2_entries = s->ct.page_sz / s->ct.entry_sz;

            l2t_addr = value & ((1ULL << 51) - 1);

            address_space_stq_le(as, l2t_addr +
                                 ((icid % max_l2_entries) * GITS_CTE_SIZE),
                                 cte, MEMTXATTRS_UNSPECIFIED, &res);
        }
    } else {
        /* Flat level table */
        address_space_stq_le(as, s->ct.base_addr + (icid * GITS_CTE_SIZE),
                             cte, MEMTXATTRS_UNSPECIFIED, &res);
    }
    return res;
}

static MemTxResult process_mapc(GICv3ITSState *s, uint32_t offset)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint16_t icid;
    uint64_t rdbase;
    bool valid;
    MemTxResult res = MEMTX_OK;
    uint64_t value;

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return res;
    }

    icid = value & ICID_MASK;

    rdbase = (value >> RDBASE_SHIFT) & RDBASE_PROCNUM_MASK;

    valid = (value >> VALID_SHIFT) & VALID_MASK;

    if ((icid > s->ct.max_collids) || (rdbase > s->gicv3->num_cpu)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "ITS MAPC: invalid collection table attributes "
            "icid %d rdbase %lu\n",  icid, rdbase);
        /*
         * in this implementation,in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
    } else {
        res = update_cte(s, icid, valid, rdbase);
    }

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    return res;
}

static MemTxResult update_dte(GICv3ITSState *s, uint32_t devid, bool valid,
    uint8_t size, uint64_t itt_addr)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t value;
    uint64_t l2t_addr;
    bool valid_l2t;
    uint32_t l2t_id;
    uint32_t max_l2_entries;
    uint64_t dte = 0;
    MemTxResult res = MEMTX_OK;

    if (s->dt.valid) {
        if (valid) {
            /* add mapping entry to device table */
            dte = (valid & VALID_MASK) |
                  ((size & SIZE_MASK) << 1U) |
                  ((itt_addr & ITTADDR_MASK) << 6ULL);
        }
    } else {
        return res;
    }

    /*
     * The specification defines the format of level 1 entries of a
     * 2-level table, but the format of level 2 entries and the format
     * of flat-mapped tables is IMPDEF.
     */
    if (s->dt.indirect) {
        l2t_id = devid / (s->dt.page_sz / L1TABLE_ENTRY_SIZE);

        value = address_space_ldq_le(as,
                                     s->dt.base_addr +
                                     (l2t_id * L1TABLE_ENTRY_SIZE),
                                     MEMTXATTRS_UNSPECIFIED, &res);

        if (res != MEMTX_OK) {
            return res;
        }

        valid_l2t = (value >> VALID_SHIFT) & VALID_MASK;

        if (valid_l2t) {
            max_l2_entries = s->dt.page_sz / s->dt.entry_sz;

            l2t_addr = value & ((1ULL << 51) - 1);

            address_space_stq_le(as, l2t_addr +
                                 ((devid % max_l2_entries) * GITS_DTE_SIZE),
                                 dte, MEMTXATTRS_UNSPECIFIED, &res);
        }
    } else {
        /* Flat level table */
        address_space_stq_le(as, s->dt.base_addr + (devid * GITS_DTE_SIZE),
                             dte, MEMTXATTRS_UNSPECIFIED, &res);
    }
    return res;
}

static MemTxResult process_mapd(GICv3ITSState *s, uint64_t value,
                                  uint32_t offset)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid;
    uint8_t size;
    uint64_t itt_addr;
    bool valid;
    MemTxResult res = MEMTX_OK;

    devid = (value >> DEVID_SHIFT) & DEVID_MASK;

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return res;
    }

    size = (value & SIZE_MASK);

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return res;
    }

    itt_addr = (value >> ITTADDR_SHIFT) & ITTADDR_MASK;

    valid = (value >> VALID_SHIFT) & VALID_MASK;

    if ((devid > s->dt.max_devids) ||
        (size > FIELD_EX64(s->typer, GITS_TYPER, IDBITS))) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "ITS MAPD: invalid device table attributes "
            "devid %d or size %d\n", devid, size);
        /*
         * in this implementation,in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
    } else {
        if (res == MEMTX_OK) {
            res = update_dte(s, devid, valid, size, itt_addr);
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
    AddressSpace *as = &s->gicv3->dma_as;
    MemTxResult res = MEMTX_OK;
    uint8_t cmd;

    if (!(s->ctlr & ITS_CTLR_ENABLED)) {
        return res;
    }

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
            res = process_int(s, data, cq_offset, INT);
            break;
        case GITS_CMD_CLEAR:
            res = process_int(s, data, cq_offset, CLEAR);
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
            res = process_mapti(s, data, cq_offset, false);
            break;
        case GITS_CMD_MAPI:
            res = process_mapti(s, data, cq_offset, true);
            break;
        case GITS_CMD_DISCARD:
            res = process_int(s, data, cq_offset, DISCARD);
            break;
        default:
            break;
        }
        if (res == MEMTX_OK) {
            rd_offset++;
            rd_offset %= s->cq.max_entries;
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, OFFSET, rd_offset);
        } else {
            /*
             * in this implementation,in case of dma read/write error
             * we stall the command processing
             */
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, STALLED, 1);
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: %x cmd processing failed!!\n", __func__, cmd);
            break;
        }
    }
    return res;
}

static bool extract_table_params(GICv3ITSState *s)
{
    bool result = true;
    uint16_t num_pages = 0;
    uint8_t  page_sz_type;
    uint8_t type;
    uint32_t page_sz = 0;
    uint64_t value;

    for (int i = 0; i < 8; i++) {
        value = s->baser[i];

        if (!value) {
            continue;
        }

        page_sz_type = FIELD_EX64(value, GITS_BASER, PAGESIZE);

        switch (page_sz_type) {
        case 0:
            page_sz = GITS_ITT_PAGE_SIZE_0;
            break;

        case 1:
            page_sz = GITS_ITT_PAGE_SIZE_1;
            break;

        case 2:
        case 3:
            page_sz = GITS_ITT_PAGE_SIZE_2;
            break;

        default:
            result = false;
            break;
        }

        if (result) {
            num_pages = FIELD_EX64(value, GITS_BASER, SIZE);

            type = FIELD_EX64(value, GITS_BASER, TYPE);

            switch (type) {

            case GITS_ITT_TYPE_DEVICE:
                memset(&s->dt, 0 , sizeof(s->dt));
                s->dt.valid = FIELD_EX64(value, GITS_BASER, VALID);

                if (s->dt.valid) {
                    s->dt.page_sz = page_sz;
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
                        s->dt.base_addr = FIELD_EX64(value, GITS_BASER,
                                                      PHYADDR);
                        s->dt.base_addr <<= R_GITS_BASER_PHYADDR_SHIFT;
                    } else if (page_sz == GITS_ITT_PAGE_SIZE_2) {
                        s->dt.base_addr = FIELD_EX64(value, GITS_BASER,
                                           PHYADDRL_64K) <<
                                           R_GITS_BASER_PHYADDRL_64K_SHIFT;
                        s->dt.base_addr |= ((value >>
                                             R_GITS_BASER_PHYADDR_SHIFT) &
                                             R_GITS_BASER_PHYADDRH_64K_MASK) <<
                                             R_GITS_BASER_PHYADDRH_64K_SHIFT;
                    }
                }
                break;

            case GITS_ITT_TYPE_COLLECTION:
                memset(&s->ct, 0 , sizeof(s->ct));
                s->ct.valid = FIELD_EX64(value, GITS_BASER, VALID);

                /*
                 * GITS_TYPER.HCC is 0 for this implementation
                 * hence writes are discarded if ct.valid is 0
                 */
                if (s->ct.valid) {
                    s->ct.page_sz = page_sz;
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
                        s->ct.max_collids = (1UL << (FIELD_EX64(s->typer,
                                                     GITS_TYPER, CIDBITS) + 1));
                    } else {
                        /* 16-bit CollectionId supported when CIL == 0 */
                        s->ct.max_collids = (1UL << 16);
                    }

                    if ((page_sz == GITS_ITT_PAGE_SIZE_0) ||
                         (page_sz == GITS_ITT_PAGE_SIZE_1)) {
                        s->ct.base_addr = FIELD_EX64(value, GITS_BASER,
                                                     PHYADDR);
                        s->ct.base_addr <<= R_GITS_BASER_PHYADDR_SHIFT;
                    } else if (page_sz == GITS_ITT_PAGE_SIZE_2) {
                        s->ct.base_addr = FIELD_EX64(value, GITS_BASER,
                                                PHYADDRL_64K) <<
                                                R_GITS_BASER_PHYADDRL_64K_SHIFT;
                        s->ct.base_addr |= ((value >>
                                             R_GITS_BASER_PHYADDR_SHIFT) &
                                             R_GITS_BASER_PHYADDRH_64K_MASK) <<
                                             R_GITS_BASER_PHYADDRH_64K_SHIFT;
                    }
                }
                break;

            default:
                break;
            }
        }
    }
    return result;
}

static void extract_cmdq_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint64_t value = s->cbaser;

    num_pages = FIELD_EX64(value, GITS_CBASER, SIZE);

    memset(&s->cq, 0 , sizeof(s->cq));
    s->cq.valid = FIELD_EX64(value, GITS_CBASER, VALID);

    if (s->cq.valid) {
        s->cq.max_entries = ((num_pages + 1) * GITS_ITT_PAGE_SIZE_0) /
                                                GITS_CMDQ_ENTRY_SIZE;
        s->cq.base_addr = FIELD_EX64(value, GITS_CBASER, PHYADDR);
        s->cq.base_addr <<= R_GITS_CBASER_PHYADDR_SHIFT;
    }
    return;
}

static MemTxResult gicv3_its_translation_write(void *opaque, hwaddr offset,
                               uint64_t data, unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    MemTxResult result = MEMTX_OK;
    uint32_t devid = 0;

    switch (offset) {
    case GITS_TRANSLATER:
        if (s->ctlr & ITS_CTLR_ENABLED) {
            devid = attrs.requester_id;
            result = process_int(s, data, devid, NONE);
        }
        break;
    default:
        break;
    }

    return result;
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

        if (s->ctlr & ITS_CTLR_ENABLED) {
            if (!extract_table_params(s)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: error extracting GITS_BASER parameters "
                    TARGET_FMT_plx "\n", __func__, offset);
            } else {
                extract_cmdq_params(s);
                s->creadr = 0;
            }
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 0, 32, value);
            s->creadr = 0;
        }
        break;
    case GITS_CBASER + 4:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = deposit64(s->cbaser, 32, 32, value);
        }
        break;
    case GITS_CWRITER:
        s->cwriter = deposit64(s->cwriter, 0, 32, value);
        if (s->cwriter != s->creadr) {
            result = process_cmdq(s);
        }
        break;
    case GITS_CWRITER + 4:
        s->cwriter = deposit64(s->cwriter, 32, 32, value);
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;

            if (offset & 7) {
                temp = s->baser[index];
                temp = deposit64(temp, 32, 32, (value & ~GITS_BASER_VAL_MASK));
                s->baser[index] |= temp;
            } else {
                s->baser[index] =  deposit64(s->baser[index], 0, 32, value);
            }
        }
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
        *data = gicv3_iidr();
        break;
    case GITS_PIDR2:
        *data = gicv3_idreg(offset - GITS_PIDR2);
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
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            index = (offset - GITS_BASER) / 8;
            s->baser[index] |= (value & ~GITS_BASER_VAL_MASK);
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & ITS_CTLR_ENABLED)) {
            s->cbaser = value;
        }
        break;
    case GITS_CWRITER:
        s->cwriter = value;
        if (s->cwriter != s->creadr) {
            result = process_cmdq(s);
        }
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
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gicv3_its_translation_ops = {
    .write_with_attrs = gicv3_its_translation_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gicv3_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);

    gicv3_its_init_mmio(s, &gicv3_its_control_ops, &gicv3_its_translation_ops);

    if (s->gicv3->cpu->gicr_typer & GICR_TYPER_PLPIS) {
        address_space_init(&s->gicv3->dma_as, s->gicv3->dma,
                           "gicv3-its-sysmem");

        /* set the ITS default features supported */
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, PHYSICAL,
                                       GITS_TYPE_PHYSICAL);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, ITT_ENTRY_SIZE,
                                       ITS_ITT_ENTRY_SIZE - 1);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, IDBITS, ITS_IDBITS);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, DEVBITS, ITS_DEVBITS);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIL, 1);
        s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIDBITS, ITS_CIDBITS);
    }
}

static void gicv3_its_reset(DeviceState *dev)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    GICv3ITSClass *c = ARM_GICV3_ITS_GET_CLASS(s);

    if (s->gicv3->cpu->gicr_typer & GICR_TYPER_PLPIS) {
        c->parent_reset(dev);

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
                                              GITS_DTE_SIZE - 1);

        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, TYPE,
                                              GITS_ITT_TYPE_COLLECTION);
        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, PAGESIZE,
                                              GITS_BASER_PAGESIZE_64K);
        s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, ENTRYSIZE,
                                              GITS_CTE_SIZE - 1);
    }
}

static void gicv3_its_post_load(GICv3ITSState *s)
{
    if (s->ctlr & ITS_CTLR_ENABLED) {
        if (!extract_table_params(s)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: error extracting GITS_BASER parameters\n", __func__);
        } else {
            extract_cmdq_params(s);
        }
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
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);

    dc->realize = gicv3_arm_its_realize;
    device_class_set_props(dc, gicv3_its_props);
    device_class_set_parent_reset(dc, gicv3_its_reset, &ic->parent_reset);
    icc->post_load = gicv3_its_post_load;
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
