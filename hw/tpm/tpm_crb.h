/*
 * tpm_crb.h - QEMU's TPM CRB interface emulator
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
#ifndef TPM_TPM_CRB_H
#define TPM_TPM_CRB_H

#include "exec/memory.h"
#include "hw/acpi/tpm.h"
#include "sysemu/tpm_backend.h"
#include "tpm_ppi.h"

#define CRB_CTRL_CMD_SIZE (TPM_CRB_ADDR_SIZE - A_CRB_DATA_BUFFER)

typedef struct TPMCRBState {
    TPMBackend *tpmbe;
    TPMBackendCmd cmd;
    MemoryRegion mmio;

    size_t be_buffer_size;

    bool ppi_enabled;
    TPMPPI ppi;
} TPMCRBState;

#define CRB_INTF_TYPE_CRB_ACTIVE 0b1
#define CRB_INTF_VERSION_CRB 0b1
#define CRB_INTF_CAP_LOCALITY_0_ONLY 0b0
#define CRB_INTF_CAP_IDLE_FAST 0b0
#define CRB_INTF_CAP_XFER_SIZE_64 0b11
#define CRB_INTF_CAP_FIFO_NOT_SUPPORTED 0b0
#define CRB_INTF_CAP_CRB_SUPPORTED 0b1
#define CRB_INTF_IF_SELECTOR_CRB 0b1

enum crb_loc_ctrl {
    CRB_LOC_CTRL_REQUEST_ACCESS = BIT(0),
    CRB_LOC_CTRL_RELINQUISH = BIT(1),
    CRB_LOC_CTRL_SEIZE = BIT(2),
    CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT = BIT(3),
};

enum crb_ctrl_req {
    CRB_CTRL_REQ_CMD_READY = BIT(0),
    CRB_CTRL_REQ_GO_IDLE = BIT(1),
};

enum crb_start {
    CRB_START_INVOKE = BIT(0),
};

enum crb_cancel {
    CRB_CANCEL_INVOKE = BIT(0),
};

#define TPM_CRB_NO_LOCALITY 0xff

void tpm_crb_request_completed(TPMCRBState *s, int ret);
enum TPMVersion tpm_crb_get_version(TPMCRBState *s);
int tpm_crb_pre_save(TPMCRBState *s);
void tpm_crb_reset(TPMCRBState *s, uint64_t baseaddr);
void tpm_crb_init_memory(Object *obj, TPMCRBState *s, Error **errp);
void tpm_crb_mem_save(TPMCRBState *s, uint32_t *saved_regs, void *saved_cmdmem);
void tpm_crb_mem_load(TPMCRBState *s, const uint32_t *saved_regs,
                      const void *saved_cmdmem);
void tpm_crb_build_aml(TPMIf *ti, Aml *scope, uint32_t baseaddr, uint32_t size,
                       bool build_ppi);

#endif /* TPM_TPM_CRB_H */
