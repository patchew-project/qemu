/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/runstate.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_mpipl.h"
#include <math.h>

#define MDST_TABLE_RELOCATED                            \
    (pnv->mpipl_state.skiboot_base + MDST_TABLE_OFF)
#define MDDT_TABLE_RELOCATED                            \
    (pnv->mpipl_state.skiboot_base + MDDT_TABLE_OFF)

/*
 * Preserve the memory regions as pointed by MDST table
 *
 * During this, the memory region pointed by entries in MDST, are 'copied'
 * as it is to the memory region pointed by corresponding entry in MDDT
 *
 * Notes: All reads should consider data coming from skiboot as bigendian,
 *        and data written should also be in big-endian
 */
static bool pnv_mpipl_preserve_mem(PnvMachineState *pnv)
{
    g_autofree MdstTableEntry *mdst = g_malloc(MDST_TABLE_SIZE);
    g_autofree MddtTableEntry *mddt = g_malloc(MDDT_TABLE_SIZE);
    g_autofree MdrtTableEntry *mdrt = g_malloc0(MDRT_TABLE_SIZE);
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;
    uint64_t src_addr, dest_addr;
    uint32_t src_len;
    uint64_t num_chunks;
    int mdrt_idx = 0;

    /* Mark the memory transactions as privileged memory access */
    attrs.user = 0;
    attrs.memory = 1;

    if (pnv->mpipl_state.mdrt_table) {
        /*
         * MDRT table allocated from some past crash, free the memory to
         * prevent memory leak
         */
        g_free(pnv->mpipl_state.mdrt_table);
        pnv->mpipl_state.num_mdrt_entries = 0;
    }

    io_result = address_space_read(default_as, MDST_TABLE_RELOCATED, attrs,
            mdst, MDST_TABLE_SIZE);
    if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "MPIPL: Failed to read MDST table at: 0x%lx\n",
            MDST_TABLE_RELOCATED);

        return false;
    }

    io_result = address_space_read(default_as, MDDT_TABLE_RELOCATED, attrs,
            mddt, MDDT_TABLE_SIZE);
    if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "MPIPL: Failed to read MDDT table at: 0x%lx\n",
            MDDT_TABLE_RELOCATED);

        return false;
    }

    /* Try to read all entries */
    for (int i = 0; i < MDST_MAX_ENTRIES; ++i) {
        g_autofree uint8_t *copy_buffer = NULL;
        bool is_copy_failed = false;

        /* Considering entry with address and size as 0, as end of table */
        if ((mdst[i].addr == 0) && (mdst[i].size == 0)) {
            break;
        }

        if (mdst[i].size != mddt[i].size) {
            qemu_log_mask(LOG_TRACE,
                    "Warning: Invalid entry, size mismatch in MDST & MDDT\n");
            continue;
        }

        if (mdst[i].data_region != mddt[i].data_region) {
            qemu_log_mask(LOG_TRACE,
                    "Warning: Invalid entry, region mismatch in MDST & MDDT\n");
            continue;
        }

        src_addr  = be64_to_cpu(mdst[i].addr) & ~HRMOR_BIT;
        dest_addr = be64_to_cpu(mddt[i].addr) & ~HRMOR_BIT;
        src_len   = be32_to_cpu(mddt[i].size);

#define COPY_CHUNK_SIZE  ((size_t)(32 * MiB))
        is_copy_failed = false;
        copy_buffer = g_try_malloc(COPY_CHUNK_SIZE);
        if (copy_buffer == NULL) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "MPIPL: Failed allocating memory (size: %zu) for copying"
                " reserved memory regions\n", COPY_CHUNK_SIZE);
        }

        num_chunks = ceil((src_len * 1.0f) / COPY_CHUNK_SIZE);
        for (uint64_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
            /* Take minimum of bytes left to copy, and chunk size */
            uint64_t copy_len = MIN(
                            src_len - (chunk_id * COPY_CHUNK_SIZE),
                            COPY_CHUNK_SIZE
                        );

            /* Copy the source region to destination */
            io_result = address_space_read(default_as, src_addr, attrs,
                    copy_buffer, copy_len);
            if (io_result != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "MPIPL: Failed to read region at: 0x%lx\n", src_addr);
                is_copy_failed = true;
                break;
            }

            io_result = address_space_write(default_as, dest_addr, attrs,
                    copy_buffer, copy_len);
            if (io_result != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "MPIPL: Failed to write region at: 0x%lx\n", dest_addr);
                is_copy_failed = true;
                break;
            }

            src_addr += COPY_CHUNK_SIZE;
            dest_addr += COPY_CHUNK_SIZE;
        }
#undef COPY_CHUNK_SIZE

        if (is_copy_failed) {
            /*
             * HDAT doesn't specify an error code in MDRT for failed copy,
             * and doesn't specify how this is to be handled
             * Hence just skip adding an entry in MDRT, as done for size
             * mismatch or other inconsistency between MDST/MDDT
             */
            continue;
        }

        /* Populate entry in MDRT table if preserving successful */
        mdrt[mdrt_idx].src_addr    = cpu_to_be64(src_addr);
        mdrt[mdrt_idx].dest_addr   = cpu_to_be64(dest_addr);
        mdrt[mdrt_idx].size        = cpu_to_be32(src_len);
        mdrt[mdrt_idx].data_region = mdst[i].data_region;
        ++mdrt_idx;
    }

    pnv->mpipl_state.mdrt_table = g_steal_pointer(&mdrt);
    pnv->mpipl_state.num_mdrt_entries = mdrt_idx;

    return true;
}

void do_mpipl_preserve(PnvMachineState *pnv)
{
    pnv_mpipl_preserve_mem(pnv);

    /* Mark next boot as Memory-preserving boot */
    pnv->mpipl_state.is_next_boot_mpipl = true;

    /*
     * Do a guest reset.
     * Next reset will see 'is_next_boot_mpipl' as true, and trigger MPIPL
     *
     * Requirement:
     * GUEST_RESET is expected to NOT clear the memory, as is the case when
     * this is merged
     */
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}
