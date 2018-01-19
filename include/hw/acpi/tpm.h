/*
 * tpm.h - TPM ACPI definitions
 *
 * Copyright (C) 2014 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org
 *
 */
#ifndef HW_ACPI_TPM_H
#define HW_ACPI_TPM_H

#include "qemu/osdep.h"

#define TPM_TIS_ADDR_BASE           0xFED40000
#define TPM_TIS_ADDR_SIZE           0x5000

#define TPM_TIS_IRQ                 5

struct crb_regs {
    union {
        uint32_t reg;
        struct {
            unsigned tpm_established:1;
            unsigned loc_assigned:1;
            unsigned active_locality:3;
            unsigned reserved:2;
            unsigned tpm_reg_valid_sts:1;
        } bits;
    } loc_state;
    uint32_t reserved1;
    uint32_t loc_ctrl;
    union {
        uint32_t reg;
        struct {
            unsigned granted:1;
            unsigned been_seized:1;
        } bits;
    } loc_sts;
    uint8_t reserved2[32];
    union {
        uint64_t reg;
        struct {
            unsigned type:4;
            unsigned version:4;
            unsigned cap_locality:1;
            unsigned cap_crb_idle_bypass:1;
            unsigned reserved1:1;
            unsigned cap_data_xfer_size_support:2;
            unsigned cap_fifo:1;
            unsigned cap_crb:1;
            unsigned cap_if_res:2;
            unsigned if_selector:2;
            unsigned if_selector_lock:1;
            unsigned reserved2:4;
            unsigned rid:8;
            unsigned vid:16;
            unsigned did:16;
        } bits;
    } intf_id;
    uint64_t ctrl_ext;

    uint32_t ctrl_req;
    union {
        uint32_t reg;
        struct {
            unsigned tpm_sts:1;
            unsigned tpm_idle:1;
            unsigned reserved:30;
        } bits;
    } ctrl_sts;
    uint32_t ctrl_cancel;
    uint32_t ctrl_start;
    uint32_t ctrl_int_enable;
    uint32_t ctrl_int_sts;
    uint32_t ctrl_cmd_size;
    uint32_t ctrl_cmd_pa_low;
    uint32_t ctrl_cmd_pa_high;
    uint32_t ctrl_rsp_size;
    uint64_t ctrl_rsp_pa;
    uint8_t reserved3[0x10];
} QEMU_PACKED;

#define TPM_CRB_ADDR_BASE           0xFED40000
#define TPM_CRB_ADDR_SIZE           0x1000
#define TPM_CRB_ADDR_CTRL \
    (TPM_CRB_ADDR_BASE + offsetof(struct crb_regs, ctrl_req))

#define TPM_LOG_AREA_MINIMUM_SIZE   (64 * 1024)

#define TPM_TCPA_ACPI_CLASS_CLIENT  0
#define TPM_TCPA_ACPI_CLASS_SERVER  1

#define TPM2_ACPI_CLASS_CLIENT      0
#define TPM2_ACPI_CLASS_SERVER      1

#define TPM2_START_METHOD_MMIO      6
#define TPM2_START_METHOD_CRB       7

#endif /* HW_ACPI_TPM_H */
