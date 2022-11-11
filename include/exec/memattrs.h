/*
 * Memory transaction attributes
 *
 * Copyright (c) 2015 Linaro Limited.
 *
 * Authors:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMATTRS_H
#define MEMATTRS_H

/**
 * typedef MemTxRequesterType - source of memory transaction
 *
 * Every memory transaction comes from a specific place which defines
 * how requester_id should be handled if at all.
 *
 * UNSPECIFIED: the default for otherwise undefined MemTxAttrs
 * CPU: requester_id is the global cpu_index
 *      This needs further processing if you need to work out which
 *      socket or complex it comes from
 * PCI: indicates the requester_id is a PCI id
 * MACHINE: indicates a machine specific encoding
 *          This will require further processing to decode into its
 *          constituent parts.
 */
typedef enum MemTxRequesterType {
    MTRT_UNSPECIFIED = 0,
    MTRT_CPU,
    MTRT_PCI,
    MTRT_MACHINE
} MemTxRequesterType;

/**
 * typedef MemTxAttrs - attributes of a memory transaction
 *
 * Every memory transaction has associated with it a set of
 * attributes. Some of these are generic (such as the ID of
 * the bus master); some are specific to a particular kind of
 * bus (such as the ARM Secure/NonSecure bit). We define them
 * all as non-overlapping bitfields in a single struct to avoid
 * confusion if different parts of QEMU used the same bit for
 * different semantics.
 */
typedef struct MemTxAttrs {
    /* Requester type (e.g. CPU or PCI MSI) */
    MemTxRequesterType requester_type:2;
    /* Requester ID */
    unsigned int requester_id:16;
    /*
     * ARM/AMBA: TrustZone Secure access
     * x86: System Management Mode access
     */
    unsigned int secure:1;
    /* Memory access is usermode (unprivileged) */
    unsigned int user:1;
    /*
     * Bus interconnect and peripherals can access anything (memories,
     * devices) by default. By setting the 'memory' bit, bus transaction
     * are restricted to "normal" memories (per the AMBA documentation)
     * versus devices. Access to devices will be logged and rejected
     * (see MEMTX_ACCESS_ERROR).
     */
    unsigned int memory:1;
    /* Invert endianness for this page */
    unsigned int byte_swap:1;
    /*
     * The following are target-specific page-table bits.  These are not
     * related to actual memory transactions at all.  However, this structure
     * is part of the tlb_fill interface, cached in the cputlb structure,
     * and has unused bits.  These fields will be read by target-specific
     * helpers using env->iotlb[mmu_idx][tlb_index()].attrs.target_tlb_bitN.
     */
    unsigned int target_tlb_bit0 : 1;
    unsigned int target_tlb_bit1 : 1;
    unsigned int target_tlb_bit2 : 1;
} MemTxAttrs;

/*
 * Bus masters which don't specify any attributes will get this which
 * indicates none of the attributes can be used.
 */
#define MEMTXATTRS_UNSPECIFIED ((MemTxAttrs) \
                                { .requester_type = MTRT_UNSPECIFIED })

/*
 * Helper for setting a basic CPU sourced transaction, it expects a
 * CPUState *
 */
#define MEMTXATTRS_CPU(cs) ((MemTxAttrs) \
                            {.requester_type = MTRT_CPU, \
                             .requester_id = cs->cpu_index})

/*
 * Helper for setting a basic PCI sourced transaction, it expects a
 * PCIDevice *
 */
#define MEMTXATTRS_PCI(dev) ((MemTxAttrs) \
                             {.requester_type = MTRT_PCI,   \
                             .requester_id = pci_requester_id(dev)})

/*
 * Helper for setting a machine specific sourced transaction. The
 * details of how to decode the requester_id are machine specific.
 */
#define MEMTXATTRS_MACHINE(id) ((MemTxAttrs) \
                                {.requester_type = MTRT_MACHINE, \
                                 .requester_id = id })

/* New-style MMIO accessors can indicate that the transaction failed.
 * A zero (MEMTX_OK) response means success; anything else is a failure
 * of some kind. The memory subsystem will bitwise-OR together results
 * if it is synthesizing an operation from multiple smaller accesses.
 */
#define MEMTX_OK 0
#define MEMTX_ERROR             (1U << 0) /* device returned an error */
#define MEMTX_DECODE_ERROR      (1U << 1) /* nothing at that address */
#define MEMTX_ACCESS_ERROR      (1U << 2) /* access denied */
typedef uint32_t MemTxResult;

#endif
