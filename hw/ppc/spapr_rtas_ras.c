/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * RAS (Reliability, Availability and Serviceability) RTAS call handlers:
 *   - ibm,configure-kernel-dump  (FADump)
 *   - ibm,os-term                (FADump-aware OS termination)
 *
 * Copyright (c) 2010-2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "system/physmem.h"
#include "system/runstate.h"
#include "kvm_ppc.h"
#include "migration/blocker.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_fadump.h"

/* PAPR Section 7.4.9 ibm,configure-kernel-dump RTAS call */
static void rtas_configure_kernel_dump(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       uint32_t token, uint32_t nargs,
                                       target_ulong args,
                                       uint32_t nret, target_ulong rets)
{
    target_ulong cmd = rtas_ld(args, 0);
    uint32_t ret_val;

    /* Number of outputs has to be 1 */
    if (nret != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: ibm,configure-kernel-dump called with nret != 1.\n");
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    /* Number of inputs has to be 3 */
    if (nargs != 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: ibm,configure-kernel-dump called with nargs != 3.\n");
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    switch (cmd) {
    case FADUMP_CMD_REGISTER:
        ret_val = do_fadump_register(spapr, args);
        if (ret_val != RTAS_OUT_SUCCESS) {
            rtas_st(rets, 0, ret_val);
            return;
        }
        break;
    case FADUMP_CMD_UNREGISTER:
        if (spapr->fadump_dump_active) {
            rtas_st(rets, 0, RTAS_OUT_DUMP_ACTIVE);
            return;
        }

        spapr->fadump_registered = false;
        spapr->fadump_dump_active = false;
        memset(&spapr->registered_fdm, 0, sizeof(spapr->registered_fdm));
        break;
    case FADUMP_CMD_INVALIDATE:
        if (!spapr->fadump_dump_active) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Nothing to invalidate, no dump active\n");

            rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        }

        spapr->fadump_registered = false;
        spapr->fadump_dump_active = false;
        memset(&spapr->registered_fdm, 0, sizeof(spapr->registered_fdm));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Unknown command: " TARGET_FMT_lu "\n", cmd);

        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_ibm_os_term(PowerPCCPU *cpu,
                             SpaprMachineState *spapr,
                             uint32_t token, uint32_t nargs,
                             target_ulong args,
                             uint32_t nret, target_ulong rets)
{
    target_ulong msgaddr = rtas_ld(args, 0);
    char msg[512];

    if (spapr->fadump_registered) {
        /* If fadump boot works, control won't come back here */
        return trigger_fadump_boot(spapr, rets);
    }

    physical_memory_read(msgaddr, msg, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;

    error_report("OS terminated: %s", msg);
    qemu_system_guest_panicked(NULL);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_ibm_nmi_register(PowerPCCPU *cpu,
                                  SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args,
                                  uint32_t nret, target_ulong rets)
{
    hwaddr rtas_addr;
    target_ulong sreset_addr, mce_addr;

    if (spapr_get_cap(spapr, SPAPR_CAP_FWNMI) == SPAPR_CAP_OFF) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    rtas_addr = spapr_get_rtas_addr();
    if (!rtas_addr) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    sreset_addr = rtas_ld(args, 0);
    mce_addr = rtas_ld(args, 1);

    /* PAPR requires these are in the first 32M of memory and within RMA */
    if (sreset_addr >= 32 * MiB || sreset_addr >= spapr->rma_size ||
           mce_addr >= 32 * MiB ||    mce_addr >= spapr->rma_size) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (kvm_enabled()) {
        if (kvmppc_set_fwnmi(cpu) < 0) {
            rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
            return;
        }
    }

    spapr->fwnmi_system_reset_addr = sreset_addr;
    spapr->fwnmi_machine_check_addr = mce_addr;

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_ibm_nmi_interlock(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   uint32_t token, uint32_t nargs,
                                   target_ulong args,
                                   uint32_t nret, target_ulong rets)
{
    if (spapr_get_cap(spapr, SPAPR_CAP_FWNMI) == SPAPR_CAP_OFF) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    if (spapr->fwnmi_machine_check_addr == -1) {
        qemu_log_mask(LOG_GUEST_ERROR,
"FWNMI: ibm,nmi-interlock RTAS called with FWNMI not registered.\n");

        /* NMI register not called */
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (spapr->fwnmi_machine_check_interlock != cpu->vcpu_id) {
        /*
         * The vCPU that hit the NMI should invoke "ibm,nmi-interlock"
         * This should be PARAM_ERROR, but Linux calls "ibm,nmi-interlock"
         * for system reset interrupts, despite them not being interlocked.
         * PowerVM silently ignores this and returns success here. Returning
         * failure causes Linux to print the error "FWNMI: nmi-interlock
         * failed: -3", although no other apparent ill effects, this is a
         * regression for the user when enabling FWNMI. So for now, match
         * PowerVM. When most Linux clients are fixed, this could be
         * changed.
         */
        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
        return;
    }

    /*
     * vCPU issuing "ibm,nmi-interlock" is done with NMI handling,
     * hence unset fwnmi_machine_check_interlock.
     */
    spapr->fwnmi_machine_check_interlock = -1;
    qemu_cond_signal(&spapr->fwnmi_machine_check_interlock_cond);
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    migrate_del_blocker(&spapr->fwnmi_migration_blocker);
}

static void ras_rtas_register_types(void)
{
    spapr_rtas_register(RTAS_IBM_OS_TERM, "ibm,os-term",
                        rtas_ibm_os_term);
    spapr_rtas_register(RTAS_IBM_NMI_REGISTER, "ibm,nmi-register",
                        rtas_ibm_nmi_register);
    spapr_rtas_register(RTAS_IBM_NMI_INTERLOCK, "ibm,nmi-interlock",
                        rtas_ibm_nmi_interlock);
    spapr_rtas_register(RTAS_CONFIGURE_KERNEL_DUMP, "ibm,configure-kernel-dump",
                        rtas_configure_kernel_dump);
}

type_init(ras_rtas_register_types)
