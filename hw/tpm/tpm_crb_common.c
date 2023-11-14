/*
 * tpm_crb.c - QEMU's TPM CRB interface emulator
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * tpm_crb is a device for TPM 2.0 Command Response Buffer (CRB) Interface
 * as defined in TCG PC Client Platform TPM Profile (PTP) Specification
 * Family “2.0” Level 00 Revision 01.03 v22
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"
#include "tpm_prop.h"
#include "tpm_ppi.h"
#include "trace.h"
#include "qom/object.h"
#include "tpm_crb.h"

static uint64_t tpm_crb_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    TPMCRBState *s = opaque;
    void *regs = (void *)&s->regs + (addr & ~3);
    unsigned offset = addr & 3;
    uint32_t val = *(uint32_t *)regs >> (8 * offset);

    switch (addr) {
    case A_CRB_LOC_STATE:
        val |= !tpm_backend_get_tpm_established_flag(s->tpmbe);
        break;
    }

    trace_tpm_crb_mmio_read(addr, size, val);

    return val;
}

static uint8_t tpm_crb_get_active_locty(TPMCRBState *s)
{
    if (!ARRAY_FIELD_EX32(s->regs, CRB_LOC_STATE, locAssigned)) {
        return TPM_CRB_NO_LOCALITY;
    }
    return ARRAY_FIELD_EX32(s->regs, CRB_LOC_STATE, activeLocality);
}

static void tpm_crb_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    TPMCRBState *s = opaque;
    uint8_t locty =  addr >> 12;

    trace_tpm_crb_mmio_write(addr, size, val);

    switch (addr) {
    case A_CRB_CTRL_REQ:
        switch (val) {
        case CRB_CTRL_REQ_CMD_READY:
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                             tpmIdle, 0);
            break;
        case CRB_CTRL_REQ_GO_IDLE:
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                             tpmIdle, 1);
            break;
        }
        break;
    case A_CRB_CTRL_CANCEL:
        if (val == CRB_CANCEL_INVOKE &&
            s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) {
            tpm_backend_cancel_cmd(s->tpmbe);
        }
        break;
    case A_CRB_CTRL_START:
        if (val == CRB_START_INVOKE &&
            !(s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) &&
            tpm_crb_get_active_locty(s) == locty) {
            void *mem = memory_region_get_ram_ptr(&s->cmdmem);

            s->regs[R_CRB_CTRL_START] |= CRB_START_INVOKE;
            s->cmd = (TPMBackendCmd) {
                .in = mem,
                .in_len = MIN(tpm_cmd_get_size(mem), s->be_buffer_size),
                .out = mem,
                .out_len = s->be_buffer_size,
            };

            tpm_backend_deliver_request(s->tpmbe, &s->cmd);
        }
        break;
    case A_CRB_LOC_CTRL:
        switch (val) {
        case CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT:
            /* not loc 3 or 4 */
            break;
        case CRB_LOC_CTRL_RELINQUISH:
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                             locAssigned, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             Granted, 0);
            break;
        case CRB_LOC_CTRL_REQUEST_ACCESS:
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             Granted, 1);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             beenSeized, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                             locAssigned, 1);
            break;
        }
        break;
    }
}

const MemoryRegionOps tpm_crb_memory_ops = {
    .read = tpm_crb_mmio_read,
    .write = tpm_crb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

void tpm_crb_request_completed(TPMCRBState *s, int ret)
{
    s->regs[R_CRB_CTRL_START] &= ~CRB_START_INVOKE;
    if (ret != 0) {
        ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                         tpmSts, 1); /* fatal error */
    }
    memory_region_set_dirty(&s->cmdmem, 0, CRB_CTRL_CMD_SIZE);
}

enum TPMVersion tpm_crb_get_version(TPMCRBState *s)
{
    return tpm_backend_get_tpm_version(s->tpmbe);
}

int tpm_crb_pre_save(TPMCRBState *s)
{
    tpm_backend_finish_sync(s->tpmbe);

    return 0;
}

void tpm_crb_reset(TPMCRBState *s, uint64_t baseaddr)
{
    if (s->ppi_enabled) {
        tpm_ppi_reset(&s->ppi);
    }
    tpm_backend_reset(s->tpmbe);

    memset(s->regs, 0, sizeof(s->regs));

    ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                     tpmRegValidSts, 1);
    ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                     tpmIdle, 1);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceType, CRB_INTF_TYPE_CRB_ACTIVE);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceVersion, CRB_INTF_VERSION_CRB);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapLocality, CRB_INTF_CAP_LOCALITY_0_ONLY);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapCRBIdleBypass, CRB_INTF_CAP_IDLE_FAST);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapDataXferSizeSupport, CRB_INTF_CAP_XFER_SIZE_64);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapFIFO, CRB_INTF_CAP_FIFO_NOT_SUPPORTED);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapCRB, CRB_INTF_CAP_CRB_SUPPORTED);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceSelector, CRB_INTF_IF_SELECTOR_CRB);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     RID, 0b0000);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID2,
                     VID, PCI_VENDOR_ID_IBM);

    baseaddr += A_CRB_DATA_BUFFER;
    s->regs[R_CRB_CTRL_CMD_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_CMD_LADDR] = (uint32_t)baseaddr;
    s->regs[R_CRB_CTRL_CMD_HADDR] = (uint32_t)(baseaddr >> 32);
    s->regs[R_CRB_CTRL_RSP_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_RSP_LADDR] = (uint32_t)baseaddr;
    s->regs[R_CRB_CTRL_RSP_HADDR] = (uint32_t)(baseaddr >> 32);

    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->tpmbe),
                            CRB_CTRL_CMD_SIZE);

    if (tpm_backend_startup_tpm(s->tpmbe, s->be_buffer_size) < 0) {
        exit(1);
    }
}

void tpm_crb_init_memory(Object *obj, TPMCRBState *s, Error **errp)
{
    memory_region_init_io(&s->mmio, obj, &tpm_crb_memory_ops, s,
        "tpm-crb-mmio", sizeof(s->regs));
    memory_region_init_ram(&s->cmdmem, obj,
        "tpm-crb-cmd", CRB_CTRL_CMD_SIZE, errp);
    if (s->ppi_enabled) {
        tpm_ppi_init_memory(&s->ppi, obj);
    }
}
