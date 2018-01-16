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

#define TPM_TIS_ADDR_BASE           0xFED40000
#define TPM_TIS_ADDR_SIZE           0x5000

#define TPM_TIS_IRQ                 5

#define TPM_LOG_AREA_MINIMUM_SIZE   (64 * 1024)

#define TPM_TCPA_ACPI_CLASS_CLIENT  0
#define TPM_TCPA_ACPI_CLASS_SERVER  1

#define TPM2_ACPI_CLASS_CLIENT      0
#define TPM2_ACPI_CLASS_SERVER      1

#define TPM2_START_METHOD_MMIO      6

/*
 * Physical Presence Interface
 */
#define TPM_PPI_ADDR_SIZE           0x400
#define TPM_PPI_ADDR_BASE           0xfffef000

struct tpm_ppi {
    uint8_t ppin;            /*  0: set by BIOS */
    uint32_t ppip;           /*  1: set by ACPI; not used */
    uint32_t pprp;           /*  5: response from TPM; set by BIOS */
    uint32_t pprq;           /*  9: opcode; set by ACPI */
    uint32_t pprm;           /* 13: parameter for opcode; set by ACPI */
    uint32_t lppr;           /* 17: last opcode; set by BIOS */
    uint32_t fret;           /* 21: set by ACPI; not used */
    uint8_t res1;            /* 25: reserved */
    uint32_t res2[4];        /* 26: reserved */
    uint8_t  res3[214];      /* 42: reserved */
    uint8_t  func[256];      /* 256: per TPM function implementation flags;
                                     set by BIOS */
/* indication whether function is implemented; bit 0 */
#define TPM_PPI_FUNC_IMPLEMENTED       (1 << 0)
/* actions OS should take to transition to the pre-OS env.; bits 1, 2 */
#define TPM_PPI_FUNC_ACTION_SHUTDOWN   (1 << 1)
#define TPM_PPI_FUNC_ACTION_REBOOT     (2 << 1)
#define TPM_PPI_FUNC_ACTION_VENDOR     (3 << 1)
#define TPM_PPI_FUNC_ACTION_MASK       (3 << 1)
/* whether function is blocked by BIOS settings; bits 3,4,5 */
#define TPM_PPI_FUNC_NOT_IMPLEMENTED     (0 << 3)
#define TPM_PPI_FUNC_BIOS_ONLY           (1 << 3)
#define TPM_PPI_FUNC_BLOCKED             (2 << 3)
#define TPM_PPI_FUNC_ALLOWED_USR_REQ     (3 << 3)
#define TPM_PPI_FUNC_ALLOWED_USR_NOT_REQ (4 << 3)
#define TPM_PPI_FUNC_MASK                (7 << 3)
} QEMU_PACKED;

#define TPM_PPI_STRUCT_SIZE  sizeof(struct tpm_ppi)

#endif /* HW_ACPI_TPM_H */
