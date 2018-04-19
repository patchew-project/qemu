/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive_regs.h"
#include "monitor/monitor.h"


/*
 * OPAL uses the priority 7 queue to automatically escalate interrupts
 * for all other queues (DD2.X POWER9). So only priorities [0..6] are
 * allowed for the guest.
 */
static bool priority_is_valid(uint8_t priority)
{
    switch (priority) {
    case 0 ... 6:
        return true;
    case 7: /* OPAL escalation queue */
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid priority %d requested\n",
                      priority);
        return false;
    }
}

/*
 * The H_INT_GET_SOURCE_INFO hcall() is used to obtain the logical
 * real address of the MMIO page through which the Event State Buffer
 * entry associated with the value of the "lisn" parameter is managed.
 *
 * Parameters:
 * Input
 * - "flags"
 *       Bits 0-63 reserved
 * - "lisn" is per "interrupts", "interrupt-map", or
 *       "ibm,xive-lisn-ranges" properties, or as returned by the
 *       ibm,query-interrupt-source-number RTAS call, or as returned
 *       by the H_ALLOCATE_VAS_WINDOW hcall
 *
 * Output
 * - R4: "flags"
 *       Bits 0-59: Reserved
 *       Bit 60: H_INT_ESB must be used for Event State Buffer
 *               management
 *       Bit 61: 1 == LSI  0 == MSI
 *       Bit 62: the full function page supports trigger
 *       Bit 63: Store EOI Supported
 * - R5: Logical Real address of full function Event State Buffer
 *       management page, -1 if ESB hcall flag is set to 1.
 * - R6: Logical Real Address of trigger only Event State Buffer
 *       management page or -1.
 * - R7: Power of 2 page size for the ESB management pages returned in
 *       R5 and R6.
 */

#define SPAPR_XIVE_SRC_H_INT_ESB     PPC_BIT(60) /* ESB manage with H_INT_ESB */
#define SPAPR_XIVE_SRC_LSI           PPC_BIT(61) /* Virtual LSI type */
#define SPAPR_XIVE_SRC_TRIGGER       PPC_BIT(62) /* Trigger and management
                                                    on same page */
#define SPAPR_XIVE_SRC_STORE_EOI     PPC_BIT(63) /* Store EOI support */

static target_ulong h_int_get_source_info(PowerPCCPU *cpu,
                                          sPAPRMachineState *spapr,
                                          target_ulong opcode,
                                          target_ulong *args)
{
    sPAPRXive *xive = spapr->xive;
    XiveIVE *ive;
    target_ulong flags  = args[0];
    target_ulong lisn   = args[1];
    XiveSource *xsrc = &xive->source;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    ive = xive_fabric_get_ive(XIVE_FABRIC(spapr->xive), lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        return H_P2;
    }

    /* All sources are emulated under the main XIVE object and share
     * the same characteristics.
     */
    args[0] = 0;
    if (!xive_source_esb_2page(xsrc)) {
        args[0] |= SPAPR_XIVE_SRC_TRIGGER;
    }
    if (xsrc->esb_flags & XIVE_SRC_STORE_EOI) {
        args[0] |= SPAPR_XIVE_SRC_STORE_EOI;
    }
    if (xsrc->esb_flags & XIVE_SRC_H_INT_ESB) {
        args[0] |= SPAPR_XIVE_SRC_H_INT_ESB;
    }

    if (xive_source_irq_is_lsi(xsrc, lisn - xsrc->offset)) {
        args[0] |= SPAPR_XIVE_SRC_LSI;
    }

    if (!(xsrc->esb_flags & XIVE_SRC_H_INT_ESB)) {
        args[1] = xive_source_esb_mgmt(xsrc, lisn - xsrc->offset);
    } else {
        args[1] = -1;
    }

    if (xive_source_esb_2page(xsrc)) {
        args[2] = xive_source_esb_trigger(xsrc, lisn - xsrc->offset);
    } else {
        args[2] = -1;
    }

    args[3] = xsrc->esb_shift;

    return H_SUCCESS;
}

/*
 * The H_INT_SET_SOURCE_CONFIG hcall() is used to assign a Logical
 * Interrupt Source to a target. The Logical Interrupt Source is
 * designated with the "lisn" parameter and the target is designated
 * with the "target" and "priority" parameters.  Upon return from the
 * hcall(), no additional interrupts will be directed to the old EQ.
 *
 * TODO: The old EQ should be investigated for interrupts that
 * occurred prior to or during the hcall().
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-61: Reserved
 *      Bit 62: set the "eisn" in the EA
 *      Bit 63: masks the interrupt source in the hardware interrupt
 *      control structure. An interrupt masked by this mechanism will
 *      be dropped, but it's source state bits will still be
 *      set. There is no race-free way of unmasking and restoring the
 *      source. Thus this should only be used in interrupts that are
 *      also masked at the source, and only in cases where the
 *      interrupt is not meant to be used for a large amount of time
 *      because no valid target exists for it for example
 * - "lisn" is per "interrupts", "interrupt-map", or
 *      "ibm,xive-lisn-ranges" properties, or as returned by the
 *      ibm,query-interrupt-source-number RTAS call, or as returned by
 *      the H_ALLOCATE_VAS_WINDOW hcall
 * - "target" is per "ibm,ppc-interrupt-server#s" or
 *      "ibm,ppc-interrupt-gserver#s"
 * - "priority" is a valid priority not in
 *      "ibm,plat-res-int-priorities"
 * - "eisn" is the guest EISN associated with the "lisn"
 *
 * Output:
 * - None
 */

#define SPAPR_XIVE_SRC_SET_EISN PPC_BIT(62)
#define SPAPR_XIVE_SRC_MASK     PPC_BIT(63)

static target_ulong h_int_set_source_config(PowerPCCPU *cpu,
                                            sPAPRMachineState *spapr,
                                            target_ulong opcode,
                                            target_ulong *args)
{
    XiveIVE *ive;
    uint64_t new_ive;
    target_ulong flags    = args[0];
    target_ulong lisn     = args[1];
    target_ulong target   = args[2];
    target_ulong priority = args[3];
    target_ulong eisn     = args[4];
    uint32_t eq_idx;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags & ~(SPAPR_XIVE_SRC_SET_EISN | SPAPR_XIVE_SRC_MASK)) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    ive = xive_fabric_get_ive(XIVE_FABRIC(spapr->xive), lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        return H_P2;
    }

    /* priority 0xff is used to reset the IVE */
    if (priority == 0xff) {
        new_ive = IVE_VALID | IVE_MASKED;
        goto out;
    }

    if (flags & SPAPR_XIVE_SRC_MASK) {
        new_ive = ive->w | IVE_MASKED;
    } else {
        new_ive = ive->w & ~IVE_MASKED;
    }

    if (!priority_is_valid(priority)) {
        return H_P4;
    }

    /* Validate that "target" is part of the list of threads allocated
     * to the partition. For that, find the EQ corresponding to the
     * target.
     */
    eq_idx = SPAPR_XIVE_EQ_INDEX(target, priority);
    if (!xive_fabric_get_eq(XIVE_FABRIC(spapr->xive), eq_idx)) {
        return H_P3;
    }

    new_ive = SETFIELD(IVE_EQ_BLOCK, new_ive, 0ul);
    new_ive = SETFIELD(IVE_EQ_INDEX, new_ive, eq_idx);

    if (flags & SPAPR_XIVE_SRC_SET_EISN) {
        new_ive = SETFIELD(IVE_EQ_DATA, new_ive, eisn);
    }

out:
    /* And update */
    ive->w = new_ive;

    return H_SUCCESS;
}

/*
 * The H_INT_GET_SOURCE_CONFIG hcall() is used to determine to which
 * target/priority pair is assigned to the specified Logical Interrupt
 * Source.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-63 Reserved
 * - "lisn" is per "interrupts", "interrupt-map", or
 *      "ibm,xive-lisn-ranges" properties, or as returned by the
 *      ibm,query-interrupt-source-number RTAS call, or as
 *      returned by the H_ALLOCATE_VAS_WINDOW hcall
 *
 * Output:
 * - R4: Target to which the specified Logical Interrupt Source is
 *       assigned
 * - R5: Priority to which the specified Logical Interrupt Source is
 *       assigned
 * - R6: EISN for the specified Logical Interrupt Source (this will be
 *       equivalent to the LISN if not changed by H_INT_SET_SOURCE_CONFIG)
 */
static target_ulong h_int_get_source_config(PowerPCCPU *cpu,
                                            sPAPRMachineState *spapr,
                                            target_ulong opcode,
                                            target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong lisn = args[1];
    XiveIVE *ive;
    XiveEQ *eq;
    uint32_t eq_idx;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    ive = xive_fabric_get_ive(XIVE_FABRIC(spapr->xive), lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        return H_P2;
    }

    eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);
    eq = xive_fabric_get_eq(XIVE_FABRIC(spapr->xive), eq_idx);
    if (!eq) {
        /* Not sure what to return here */
        return H_HARDWARE;
    }

    args[0] = GETFIELD(EQ_W6_NVT_INDEX, eq->w6);

    if (ive->w & IVE_MASKED) {
        args[1] = 0xff;
    } else {
        args[1] = GETFIELD(EQ_W7_F0_PRIORITY, eq->w7);
    }

    args[2] = GETFIELD(IVE_EQ_DATA, ive->w);

    return H_SUCCESS;
}

/*
 * The H_INT_GET_QUEUE_INFO hcall() is used to get the logical real
 * address of the notification management page associated with the
 * specified target and priority.
 *
 * Parameters:
 * Input:
 * - "flags"
 *       Bits 0-63 Reserved
 * - "target" is per "ibm,ppc-interrupt-server#s" or
 *       "ibm,ppc-interrupt-gserver#s"
 * - "priority" is a valid priority not in
 *       "ibm,plat-res-int-priorities"
 *
 * Output:
 * - R4: Logical real address of notification page
 * - R5: Power of 2 page size of the notification page
 */
static target_ulong h_int_get_queue_info(PowerPCCPU *cpu,
                                         sPAPRMachineState *spapr,
                                         target_ulong opcode,
                                         target_ulong *args)
{
    target_ulong flags    = args[0];
    target_ulong target   = args[1];
    target_ulong priority = args[2];
    XiveEQ *eq;
    uint32_t eq_idx;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    if (!priority_is_valid(priority)) {
        return H_P3;
    }

    /* Validate that "target" is part of the list of threads allocated
     * to the partition. For that, find the EQ corresponding to the
     * target.
     */
    eq_idx = SPAPR_XIVE_EQ_INDEX(target, priority);
    eq = xive_fabric_get_eq(XIVE_FABRIC(spapr->xive), eq_idx);
    if (!eq)  {
        return H_P2;
    }

    args[0] = -1; /* TODO: return ESn page */
    if (eq->w0 & EQ_W0_ENQUEUE) {
        args[1] = GETFIELD(EQ_W0_QSIZE, eq->w0) + 12;
    } else {
        args[1] = 0;
    }

    return H_SUCCESS;
}

/*
 * The H_INT_SET_QUEUE_CONFIG hcall() is used to set or reset a EQ for
 * a given "target" and "priority".  It is also used to set the
 * notification config associated with the EQ.  An EQ size of 0 is
 * used to reset the EQ config for a given target and priority. If
 * resetting the EQ config, the END associated with the given "target"
 * and "priority" will be changed to disable queueing.
 *
 * Upon return from the hcall(), no additional interrupts will be
 * directed to the old EQ (if one was set). The old EQ (if one was
 * set) should be investigated for interrupts that occurred prior to
 * or during the hcall().
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-62: Reserved
 *      Bit 63: Unconditional Notify (n) per the XIVE spec
 * - "target" is per "ibm,ppc-interrupt-server#s" or
 *       "ibm,ppc-interrupt-gserver#s"
 * - "priority" is a valid priority not in
 *       "ibm,plat-res-int-priorities"
 * - "eventQueue": The logical real address of the start of the EQ
 * - "eventQueueSize": The power of 2 EQ size per "ibm,xive-eq-sizes"
 *
 * Output:
 * - None
 */

#define SPAPR_XIVE_EQ_ALWAYS_NOTIFY PPC_BIT(63)

static target_ulong h_int_set_queue_config(PowerPCCPU *cpu,
                                           sPAPRMachineState *spapr,
                                           target_ulong opcode,
                                           target_ulong *args)
{
    target_ulong flags    = args[0];
    target_ulong target   = args[1];
    target_ulong priority = args[2];
    target_ulong qpage    = args[3];
    target_ulong qsize    = args[4];
    XiveEQ *old_eq;
    XiveEQ eq;
    uint32_t eq_idx;
    uint32_t qdata;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags & ~SPAPR_XIVE_EQ_ALWAYS_NOTIFY) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    if (!priority_is_valid(priority)) {
        return H_P3;
    }

    /* Validate that "target" is part of the list of threads allocated
     * to the partition. For that, find the EQ corresponding to the
     * target.
     */
    eq_idx = SPAPR_XIVE_EQ_INDEX(target, priority);
    old_eq = xive_fabric_get_eq(XIVE_FABRIC(spapr->xive), eq_idx);
    if (!old_eq)  {
        return H_P2;
    }

    eq = *old_eq;

    switch (qsize) {
    case 12:
    case 16:
    case 21:
    case 24:
        eq.w3 = ((uint64_t)qpage) & 0xffffffff;
        eq.w2 = (((uint64_t)qpage)) >> 32 & 0x0fffffff;
        eq.w0 |= EQ_W0_ENQUEUE;
        eq.w0 = SETFIELD(EQ_W0_QSIZE, eq.w0, qsize - 12);
        break;
    case 0:
        /* reset queue and disable queueing */
        xive_eq_reset(&eq);
        goto out;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid EQ size %"PRIx64"\n",
                      qsize);
        return H_P5;
    }

    if (qsize) {
        /*
         * Let's validate the EQ address with a read of the first EQ
         * entry. We could also check that the full queue has been
         * zeroed by the OS.
         */
        if (address_space_read(&address_space_memory, qpage,
                               MEMTXATTRS_UNSPECIFIED,
                               (uint8_t *) &qdata, sizeof(qdata))) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to read EQ data @0x%"
                          HWADDR_PRIx "\n", qpage);
            return H_P4;
        }
    }

    /* Ensure the priority and target are correctly set (they will not
     * be right after allocation)
     */
    eq.w6 = SETFIELD(EQ_W6_NVT_BLOCK, 0ul, 0ul) |
        SETFIELD(EQ_W6_NVT_INDEX, 0ul, target);
    eq.w7 = SETFIELD(EQ_W7_F0_PRIORITY, 0ul, priority);

    /* TODO: depends on notitification page (ESn) from H_INT_GET_QUEUE_INFO */
    if (flags & SPAPR_XIVE_EQ_ALWAYS_NOTIFY) {
        eq.w0 |= EQ_W0_UCOND_NOTIFY;
    } else {
        eq.w0 &= ~EQ_W0_UCOND_NOTIFY;
    }

    /* The generation bit for the EQ starts at 1 and The EQ page
     * offset counter starts at 0.
     */
    eq.w1 = EQ_W1_GENERATION | SETFIELD(EQ_W1_PAGE_OFF, 0ul, 0ul);
    eq.w0 |= EQ_W0_VALID;

    /* TODO: issue syncs required to ensure all in-flight interrupts
     * are complete on the old EQ */
out:
    /* Update EQ */
    *old_eq = eq;

    return H_SUCCESS;
}

/*
 * The H_INT_GET_QUEUE_CONFIG hcall() is used to get a EQ for a given
 * target and priority.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-62: Reserved
 *      Bit 63: Debug: Return debug data
 * - "target" is per "ibm,ppc-interrupt-server#s" or
 *       "ibm,ppc-interrupt-gserver#s"
 * - "priority" is a valid priority not in
 *       "ibm,plat-res-int-priorities"
 *
 * Output:
 * - R4: "flags":
 *       Bits 0-61: Reserved
 *       Bit 62: The value of Event Queue Generation Number (g) per
 *              the XIVE spec if "Debug" = 1
 *       Bit 63: The value of Unconditional Notify (n) per the XIVE spec
 * - R5: The logical real address of the start of the EQ
 * - R6: The power of 2 EQ size per "ibm,xive-eq-sizes"
 * - R7: The value of Event Queue Offset Counter per XIVE spec
 *       if "Debug" = 1, else 0
 *
 */

#define SPAPR_XIVE_EQ_DEBUG     PPC_BIT(63)

static target_ulong h_int_get_queue_config(PowerPCCPU *cpu,
                                           sPAPRMachineState *spapr,
                                           target_ulong opcode,
                                           target_ulong *args)
{
    target_ulong flags    = args[0];
    target_ulong target   = args[1];
    target_ulong priority = args[2];
    XiveEQ *eq;
    uint32_t eq_idx;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags & ~SPAPR_XIVE_EQ_DEBUG) {
        return H_PARAMETER;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    if (!priority_is_valid(priority)) {
        return H_P3;
    }

    /* Validate that "target" is part of the list of threads allocated
     * to the partition. For that, find the EQ corresponding to the
     * target.
     */
    eq_idx = SPAPR_XIVE_EQ_INDEX(target, priority);
    eq = xive_fabric_get_eq(XIVE_FABRIC(spapr->xive), eq_idx);
    if (!eq)  {
        return H_P2;
    }

    args[0] = 0;
    if (eq->w0 & EQ_W0_UCOND_NOTIFY) {
        args[0] |= SPAPR_XIVE_EQ_ALWAYS_NOTIFY;
    }

    if (eq->w0 & EQ_W0_ENQUEUE) {
        args[1] =
            (((uint64_t)(eq->w2 & 0x0fffffff)) << 32) | eq->w3;
        args[2] = GETFIELD(EQ_W0_QSIZE, eq->w0) + 12;
    } else {
        args[1] = 0;
        args[2] = 0;
    }

    /* TODO: do we need any locking on the EQ ? */
    if (flags & SPAPR_XIVE_EQ_DEBUG) {
        /* Load the event queue generation number into the return flags */
        args[0] |= (uint64_t)GETFIELD(EQ_W1_GENERATION, eq->w1) << 62;

        /* Load R7 with the event queue offset counter */
        args[3] = GETFIELD(EQ_W1_PAGE_OFF, eq->w1);
    } else {
        args[3] = 0;
    }

    return H_SUCCESS;
}

/*
 * The H_INT_SET_OS_REPORTING_LINE hcall() is used to set the
 * reporting cache line pair for the calling thread.  The reporting
 * cache lines will contain the OS interrupt context when the OS
 * issues a CI store byte to @TIMA+0xC10 to acknowledge the OS
 * interrupt. The reporting cache lines can be reset by inputting -1
 * in "reportingLine".  Issuing the CI store byte without reporting
 * cache lines registered will result in the data not being accessible
 * to the OS.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-63: Reserved
 * - "reportingLine": The logical real address of the reporting cache
 *    line pair
 *
 * Output:
 * - None
 */
static target_ulong h_int_set_os_reporting_line(PowerPCCPU *cpu,
                                                sPAPRMachineState *spapr,
                                                target_ulong opcode,
                                                target_ulong *args)
{
    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    /* TODO: H_INT_SET_OS_REPORTING_LINE */
    return H_FUNCTION;
}

/*
 * The H_INT_GET_OS_REPORTING_LINE hcall() is used to get the logical
 * real address of the reporting cache line pair set for the input
 * "target".  If no reporting cache line pair has been set, -1 is
 * returned.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-63: Reserved
 * - "target" is per "ibm,ppc-interrupt-server#s" or
 *       "ibm,ppc-interrupt-gserver#s"
 * - "reportingLine": The logical real address of the reporting cache
 *   line pair
 *
 * Output:
 * - R4: The logical real address of the reporting line if set, else -1
 */
static target_ulong h_int_get_os_reporting_line(PowerPCCPU *cpu,
                                                sPAPRMachineState *spapr,
                                                target_ulong opcode,
                                                target_ulong *args)
{
    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    /* TODO: H_INT_GET_OS_REPORTING_LINE */
    return H_FUNCTION;
}

/*
 * The H_INT_ESB hcall() is used to issue a load or store to the ESB
 * page for the input "lisn".  This hcall is only supported for LISNs
 * that have the ESB hcall flag set to 1 when returned from hcall()
 * H_INT_GET_SOURCE_INFO.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-62: Reserved
 *      bit 63: Store: Store=1, store operation, else load operation
 * - "lisn" is per "interrupts", "interrupt-map", or
 *      "ibm,xive-lisn-ranges" properties, or as returned by the
 *      ibm,query-interrupt-source-number RTAS call, or as
 *      returned by the H_ALLOCATE_VAS_WINDOW hcall
 * - "esbOffset" is the offset into the ESB page for the load or store operation
 * - "storeData" is the data to write for a store operation
 *
 * Output:
 * - R4: R4: The value of the load if load operation, else -1
 */

#define SPAPR_XIVE_ESB_STORE PPC_BIT(63)

static target_ulong h_int_esb(PowerPCCPU *cpu,
                              sPAPRMachineState *spapr,
                              target_ulong opcode,
                              target_ulong *args)
{
    sPAPRXive *xive = spapr->xive;
    XiveIVE *ive;
    target_ulong flags   = args[0];
    target_ulong lisn    = args[1];
    target_ulong offset  = args[2];
    target_ulong data    = args[3];
    hwaddr mmio_addr;
    XiveSource *xsrc = &xive->source;

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags & ~SPAPR_XIVE_ESB_STORE) {
        return H_PARAMETER;
    }

    ive = xive_fabric_get_ive(XIVE_FABRIC(xive), lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        return H_P2;
    }

    if (offset > (1ull << xsrc->esb_shift)) {
        return H_P3;
    }

    mmio_addr = xive_source_esb_base(xsrc, lisn - xsrc->offset) + offset;

    if (dma_memory_rw(&address_space_memory, mmio_addr, &data, 8,
                      (flags & SPAPR_XIVE_ESB_STORE))) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to access ESB @0x%"
                      HWADDR_PRIx "\n", mmio_addr);
        return H_HARDWARE;
    }
    args[0] = (flags & SPAPR_XIVE_ESB_STORE) ? -1 : data;
    return H_SUCCESS;
}

/*
 * The H_INT_SYNC hcall() is used to issue hardware syncs that will
 * ensure any in flight events for the input lisn are in the event
 * queue.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-63: Reserved
 * - "lisn" is per "interrupts", "interrupt-map", or
 *      "ibm,xive-lisn-ranges" properties, or as returned by the
 *      ibm,query-interrupt-source-number RTAS call, or as
 *      returned by the H_ALLOCATE_VAS_WINDOW hcall
 *
 * Output:
 * - None
 */
static target_ulong h_int_sync(PowerPCCPU *cpu,
                               sPAPRMachineState *spapr,
                               target_ulong opcode,
                               target_ulong *args)
{
    XiveIVE *ive;
    target_ulong flags   = args[0];
    target_ulong lisn    = args[1];

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags) {
        return H_PARAMETER;
    }

    ive = xive_fabric_get_ive(XIVE_FABRIC(spapr->xive), lisn);
    if (!ive || !(ive->w & IVE_VALID)) {
        return H_P2;
    }

    /*
     * H_STATE should be returned if a H_INT_RESET is in progress.
     * This is not needed when running the emulation under QEMU
     */

    /* This is not real hardware. Nothing to be done */
    return H_SUCCESS;
}

/*
 * The H_INT_RESET hcall() is used to reset all of the partition's
 * interrupt exploitation structures to their initial state.  This
 * means losing all previously set interrupt state set via
 * H_INT_SET_SOURCE_CONFIG and H_INT_SET_QUEUE_CONFIG.
 *
 * Parameters:
 * Input:
 * - "flags"
 *      Bits 0-63: Reserved
 *
 * Output:
 * - None
 */
static target_ulong h_int_reset(PowerPCCPU *cpu,
                                sPAPRMachineState *spapr,
                                target_ulong opcode,
                                target_ulong *args)
{
    target_ulong flags   = args[0];

    if (!spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        return H_FUNCTION;
    }

    if (flags) {
        return H_PARAMETER;
    }

    device_reset(DEVICE(spapr->xive));
    return H_SUCCESS;
}

void spapr_xive_hcall_init(sPAPRMachineState *spapr)
{
    spapr_register_hypercall(H_INT_GET_SOURCE_INFO, h_int_get_source_info);
    spapr_register_hypercall(H_INT_SET_SOURCE_CONFIG, h_int_set_source_config);
    spapr_register_hypercall(H_INT_GET_SOURCE_CONFIG, h_int_get_source_config);
    spapr_register_hypercall(H_INT_GET_QUEUE_INFO, h_int_get_queue_info);
    spapr_register_hypercall(H_INT_SET_QUEUE_CONFIG, h_int_set_queue_config);
    spapr_register_hypercall(H_INT_GET_QUEUE_CONFIG, h_int_get_queue_config);
    spapr_register_hypercall(H_INT_SET_OS_REPORTING_LINE,
                             h_int_set_os_reporting_line);
    spapr_register_hypercall(H_INT_GET_OS_REPORTING_LINE,
                             h_int_get_os_reporting_line);
    spapr_register_hypercall(H_INT_ESB, h_int_esb);
    spapr_register_hypercall(H_INT_SYNC, h_int_sync);
    spapr_register_hypercall(H_INT_RESET, h_int_reset);
}

void spapr_dt_xive(sPAPRMachineState *spapr, int nr_servers,
                   void *fdt, uint32_t phandle)
{
    sPAPRXive *xive = spapr->xive;
    int node;
    uint64_t timas[2 * 2];
    /* Interrupt number ranges for the IPIs */
    uint32_t lisn_ranges[] = {
        cpu_to_be32(0),
        cpu_to_be32(nr_servers),
    };
    uint32_t eq_sizes[] = {
        cpu_to_be32(12), /* 4K */
        cpu_to_be32(16), /* 64K */
        cpu_to_be32(21), /* 2M */
        cpu_to_be32(24), /* 16M */
    };
    /* The following array is in sync with the 'priority_is_valid'
     * routine above. Linux is expected to choose priority 6.
     */
    uint32_t plat_res_int_priorities[] = {
        cpu_to_be32(7),    /* start */
        cpu_to_be32(0xf8), /* count */
    };
    int i;
    gchar *nodename;

    /* Thread Interrupt Management Area : User and OS views */
    for (i = 0; i < 2; i++) {
        timas[i * 2] = cpu_to_be64(xive->tm_base + i * (1ull << TM_SHIFT));
        timas[i * 2 + 1] = cpu_to_be64(1ull << TM_SHIFT);
    }

    nodename = g_strdup_printf("interrupt-controller@%" PRIx64, xive->tm_base);
    _FDT(node = fdt_add_subnode(fdt, 0, nodename));
    g_free(nodename);

    _FDT(fdt_setprop_string(fdt, node, "device_type", "power-ivpe"));
    _FDT(fdt_setprop(fdt, node, "reg", timas, sizeof(timas)));

    _FDT(fdt_setprop_string(fdt, node, "compatible", "ibm,power-ivpe"));
    _FDT(fdt_setprop(fdt, node, "ibm,xive-eq-sizes", eq_sizes,
                     sizeof(eq_sizes)));
    _FDT(fdt_setprop(fdt, node, "ibm,xive-lisn-ranges", lisn_ranges,
                     sizeof(lisn_ranges)));

    /* For Linux to link the LSIs to the main interrupt controller.
     * These properties are not in XIVE exploitation mode sPAPR
     * specs
     */
    _FDT(fdt_setprop(fdt, node, "interrupt-controller", NULL, 0));
    _FDT(fdt_setprop_cell(fdt, node, "#interrupt-cells", 2));

    /* For SLOF */
    _FDT(fdt_setprop_cell(fdt, node, "linux,phandle", phandle));
    _FDT(fdt_setprop_cell(fdt, node, "phandle", phandle));

    /* The "ibm,plat-res-int-priorities" property defines the priority
     * ranges reserved by the hypervisor
     */
    _FDT(fdt_setprop(fdt, 0, "ibm,plat-res-int-priorities",
                     plat_res_int_priorities, sizeof(plat_res_int_priorities)));
}
