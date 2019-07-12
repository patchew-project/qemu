/*
 * SPAPR TPM Hypercall
 *
 * Copyright IBM Corp. 2019
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "hw/ppc/spapr.h"
#include "trace.h"

#define TPM_SPAPR_BUFSIZE 4096

enum {
    TPM_COMM_OP_EXECUTE = 1,
    TPM_COMM_OP_CLOSE_SESSION = 2,
};

static int tpm_devfd = -1;

static ssize_t tpm_execute(SpaprMachineState *spapr, target_ulong *args)
{
    uint64_t data_in = ppc64_phys_to_real(args[1]);
    target_ulong data_in_size = args[2];
    uint64_t data_out = ppc64_phys_to_real(args[3]);
    target_ulong data_out_size = args[4];
    uint8_t buf_in[TPM_SPAPR_BUFSIZE];
    uint8_t buf_out[TPM_SPAPR_BUFSIZE];
    ssize_t ret;

    trace_spapr_tpm_execute(data_in, data_in_size, data_out, data_out_size);

    if (data_in_size > TPM_SPAPR_BUFSIZE) {
        error_report("invalid TPM input buffer size: " TARGET_FMT_lu "\n",
                     data_in_size);
        return H_P3;
    }

    if (data_out_size < TPM_SPAPR_BUFSIZE) {
        error_report("invalid TPM output buffer size: " TARGET_FMT_lu "\n",
                     data_out_size);
        return H_P5;
    }

    if (tpm_devfd == -1) {
        tpm_devfd = open(spapr->tpm_device_file, O_RDWR);
        if (tpm_devfd == -1) {
            error_report("failed to open TPM device %s: %d",
                         spapr->tpm_device_file, errno);
            return H_RESOURCE;
        }
    }

    cpu_physical_memory_read(data_in, buf_in, data_in_size);

    do {
        ret = write(tpm_devfd, buf_in, data_in_size);
        if (ret > 0) {
            data_in_size -= ret;
        }
    } while ((ret >= 0 && data_in_size > 0) || (ret == -1 && errno == EINTR));

    if (ret == -1) {
        error_report("failed to write to TPM device %s: %d",
                     spapr->tpm_device_file, errno);
        return H_RESOURCE;
    }

    do {
        ret = read(tpm_devfd, buf_out, data_out_size);
    } while (ret == 0 || (ret == -1 && errno == EINTR));

    if (ret == -1) {
        error_report("failed to read from TPM device %s: %d",
                     spapr->tpm_device_file, errno);
        return H_RESOURCE;
    }

    cpu_physical_memory_write(data_out, buf_out, ret);
    args[0] = ret;

    return H_SUCCESS;
}

static target_ulong h_tpm_comm(PowerPCCPU *cpu,
                               SpaprMachineState *spapr,
                               target_ulong opcode,
                               target_ulong *args)
{
    target_ulong op = args[0];

    trace_spapr_h_tpm_comm(spapr->tpm_device_file ?: "null", op);

    if (!spapr->tpm_device_file) {
        error_report("TPM device not specified");
        return H_RESOURCE;
    }

    switch (op) {
        case TPM_COMM_OP_EXECUTE:
            return tpm_execute(spapr, args);
        case TPM_COMM_OP_CLOSE_SESSION:
            if (tpm_devfd != -1) {
                close(tpm_devfd);
                tpm_devfd = -1;
            }
            return H_SUCCESS;
        default:
            return H_PARAMETER;
    }
}

void spapr_hcall_tpm_reset(void)
{
    if (tpm_devfd != -1) {
        close(tpm_devfd);
        tpm_devfd = -1;
    }
}

static void hypercall_register_types(void)
{
    spapr_register_hypercall(H_TPM_COMM, h_tpm_comm);
}

type_init(hypercall_register_types)
