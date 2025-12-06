/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include "system/runstate.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_mpipl.h"
#include <math.h>

#define MDST_TABLE_RELOCATED                            \
    (pnv->mpipl_state.skiboot_base + MDST_TABLE_OFF)
#define MDDT_TABLE_RELOCATED                            \
    (pnv->mpipl_state.skiboot_base + MDDT_TABLE_OFF)
#define PROC_DUMP_RELOCATED                             \
    (pnv->mpipl_state.skiboot_base + PROC_DUMP_AREA_OFF)

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

static void do_store_cpu_regs(CPUState *cpu, MpiplPreservedCPUState *state)
{
    CPUPPCState *env = cpu_env(cpu);
    MpiplRegDataHdr *regs_hdr = &state->hdr;
    MpiplRegEntry *reg_entries = state->reg_entries;
    MpiplRegEntry *curr_reg_entry;
    uint32_t num_saved_regs = 0;

    cpu_synchronize_state(cpu);

    regs_hdr->pir = cpu_to_be32(env->spr[SPR_PIR]);

    /* QEMU CPUs are not in Power Saving Mode */
    regs_hdr->core_state = 0xff;

    regs_hdr->off_regentries = 0;
    regs_hdr->num_regentries = cpu_to_be32(NUM_REGS_PER_CPU);

    regs_hdr->alloc_size = cpu_to_be32(sizeof(MpiplRegEntry));
    regs_hdr->act_size   = cpu_to_be32(sizeof(MpiplRegEntry));

#define REG_TYPE_GPR  0x1
#define REG_TYPE_SPR  0x2
#define REG_TYPE_TIMA 0x3

/*
 * ID numbers used by f/w while populating certain registers
 *
 * Copied these defines from the linux kernel
 */
#define REG_ID_NIP          0x7D0
#define REG_ID_MSR          0x7D1
#define REG_ID_CCR          0x7D2

    curr_reg_entry = reg_entries;

#define REG_ENTRY(type, num, val)                          \
    do {                                               \
        curr_reg_entry->reg_type = cpu_to_be32(type);  \
        curr_reg_entry->reg_num  = cpu_to_be32(num);   \
        curr_reg_entry->reg_val  = cpu_to_be64(val);   \
        ++curr_reg_entry;                              \
        ++num_saved_regs;                            \
    } while (0)

    /* Save the GPRs */
    for (int gpr_id = 0; gpr_id < 32; ++gpr_id) {
        REG_ENTRY(REG_TYPE_GPR, gpr_id, env->gpr[gpr_id]);
    }

    REG_ENTRY(REG_TYPE_SPR, REG_ID_NIP, env->nip);
    REG_ENTRY(REG_TYPE_SPR, REG_ID_MSR, env->msr);

    /*
     * Ensure the number of registers saved match the number of
     * registers per cpu
     *
     * This will help catch an error if in future a new register entry
     * is added/removed while not modifying NUM_PER_CPU_REGS
     */
    assert(num_saved_regs == NUM_REGS_PER_CPU);
}

static void pnv_mpipl_preserve_cpu_state(PnvMachineState *pnv)
{
    MachineState *machine = MACHINE(pnv);
    uint32_t num_cpus = machine->smp.cpus;
    MpiplPreservedCPUState *state;
    CPUState *cpu;

    if (pnv->mpipl_state.cpu_states) {
        /*
         * CPU States might have been allocated from some past crash, free the
         * memory to preven memory leak
         */
        g_free(pnv->mpipl_state.cpu_states);
        pnv->mpipl_state.num_cpu_states = 0;
    }

    pnv->mpipl_state.cpu_states = g_malloc_n(num_cpus,
            sizeof(MpiplPreservedCPUState));
    pnv->mpipl_state.num_cpu_states = num_cpus;

    state = pnv->mpipl_state.cpu_states;

    /* Preserve the Processor Dump Area */
    cpu_physical_memory_read(PROC_DUMP_RELOCATED, &pnv->mpipl_state.proc_area,
            sizeof(MpiplProcDumpArea));

    CPU_FOREACH(cpu) {
        do_store_cpu_regs(cpu, state);
        ++state;
    }
}

void do_mpipl_preserve(PnvMachineState *pnv)
{
    pause_all_vcpus();

    pnv_mpipl_preserve_mem(pnv);
    pnv_mpipl_preserve_cpu_state(pnv);

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
