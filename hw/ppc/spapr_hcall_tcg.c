#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "helper_regs.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "mmu-hash64.h"
#include "mmu-misc.h"
#include "cpu-models.h"
#include "trace.h"
#include "kvm_ppc.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/spapr_ovec.h"
#include "mmu-book3s-v3.h"
#include "hw/mem/memory-device.h"

static inline bool valid_ptex(PowerPCCPU *cpu, target_ulong ptex)
{
    /*
     * hash value/pteg group index is normalized by HPT mask
     */
    if (((ptex & ~7ULL) / HPTES_PER_GROUP) & ~ppc_hash64_hpt_mask(cpu)) {
        return false;
    }
    return true;
}

static bool is_ram_address(SpaprMachineState *spapr, hwaddr addr)
{
    MachineState *machine = MACHINE(spapr);
    DeviceMemoryState *dms = machine->device_memory;

    if (addr < machine->ram_size) {
        return true;
    }
    if ((addr >= dms->base)
        && ((addr - dms->base) < memory_region_size(&dms->mr))) {
        return true;
    }

    return false;
}

static target_ulong h_enter(PowerPCCPU *cpu, SpaprMachineState *spapr,
                            target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong pteh = args[2];
    target_ulong ptel = args[3];
    unsigned apshift;
    target_ulong raddr;
    target_ulong slot;
    const ppc_hash_pte64_t *hptes;

    apshift = ppc_hash64_hpte_page_shift_noslb(cpu, pteh, ptel);
    if (!apshift) {
        /* Bad page size encoding */
        return H_PARAMETER;
    }

    raddr = (ptel & HPTE64_R_RPN) & ~((1ULL << apshift) - 1);

    if (is_ram_address(spapr, raddr)) {
        /* Regular RAM - should have WIMG=0010 */
        if ((ptel & HPTE64_R_WIMG) != HPTE64_R_M) {
            return H_PARAMETER;
        }
    } else {
        target_ulong wimg_flags;
        /* Looks like an IO address */
        /* FIXME: What WIMG combinations could be sensible for IO?
         * For now we allow WIMG=010x, but are there others? */
        /* FIXME: Should we check against registered IO addresses? */
        wimg_flags = (ptel & (HPTE64_R_W | HPTE64_R_I | HPTE64_R_M));

        if (wimg_flags != HPTE64_R_I &&
            wimg_flags != (HPTE64_R_I | HPTE64_R_M)) {
            return H_PARAMETER;
        }
    }

    pteh &= ~0x60ULL;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    slot = ptex & 7ULL;
    ptex = ptex & ~7ULL;

    if (likely((flags & H_EXACT) == 0)) {
        hptes = ppc_hash64_map_hptes(cpu, ptex, HPTES_PER_GROUP);
        for (slot = 0; slot < 8; slot++) {
            if (!(ppc_hash64_hpte0(cpu, hptes, slot) & HPTE64_V_VALID)) {
                break;
            }
        }
        ppc_hash64_unmap_hptes(cpu, hptes, ptex, HPTES_PER_GROUP);
        if (slot == 8) {
            return H_PTEG_FULL;
        }
    } else {
        hptes = ppc_hash64_map_hptes(cpu, ptex + slot, 1);
        if (ppc_hash64_hpte0(cpu, hptes, 0) & HPTE64_V_VALID) {
            ppc_hash64_unmap_hptes(cpu, hptes, ptex + slot, 1);
            return H_PTEG_FULL;
        }
        ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);
    }

    spapr_store_hpte(cpu, ptex + slot, pteh | HPTE64_V_HPTE_DIRTY, ptel);

    args[0] = ptex + slot;
    return H_SUCCESS;
}

typedef enum {
    REMOVE_SUCCESS = 0,
    REMOVE_NOT_FOUND = 1,
    REMOVE_PARM = 2,
    REMOVE_HW = 3,
} RemoveResult;

static RemoveResult remove_hpte(PowerPCCPU *cpu
                                , target_ulong ptex,
                                target_ulong avpn,
                                target_ulong flags,
                                target_ulong *vp, target_ulong *rp)
{
    const ppc_hash_pte64_t *hptes;
    target_ulong v, r;

    if (!valid_ptex(cpu, ptex)) {
        return REMOVE_PARM;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, 1);
    v = ppc_hash64_hpte0(cpu, hptes, 0);
    r = ppc_hash64_hpte1(cpu, hptes, 0);
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn) ||
        ((flags & H_ANDCOND) && (v & avpn) != 0)) {
        return REMOVE_NOT_FOUND;
    }
    *vp = v;
    *rp = r;
    spapr_store_hpte(cpu, ptex, HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, ptex, v, r);
    return REMOVE_SUCCESS;
}

static target_ulong h_remove(PowerPCCPU *cpu, SpaprMachineState *spapr,
                             target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong avpn = args[2];
    RemoveResult ret;

    ret = remove_hpte(cpu, ptex, avpn, flags,
                      &args[0], &args[1]);

    switch (ret) {
    case REMOVE_SUCCESS:
        check_tlb_flush(env, true);
        return H_SUCCESS;

    case REMOVE_NOT_FOUND:
        return H_NOT_FOUND;

    case REMOVE_PARM:
        return H_PARAMETER;

    case REMOVE_HW:
        return H_HARDWARE;
    }

    g_assert_not_reached();
}

#define H_BULK_REMOVE_TYPE             0xc000000000000000ULL
#define   H_BULK_REMOVE_REQUEST        0x4000000000000000ULL
#define   H_BULK_REMOVE_RESPONSE       0x8000000000000000ULL
#define   H_BULK_REMOVE_END            0xc000000000000000ULL
#define H_BULK_REMOVE_CODE             0x3000000000000000ULL
#define   H_BULK_REMOVE_SUCCESS        0x0000000000000000ULL
#define   H_BULK_REMOVE_NOT_FOUND      0x1000000000000000ULL
#define   H_BULK_REMOVE_PARM           0x2000000000000000ULL
#define   H_BULK_REMOVE_HW             0x3000000000000000ULL
#define H_BULK_REMOVE_RC               0x0c00000000000000ULL
#define H_BULK_REMOVE_FLAGS            0x0300000000000000ULL
#define   H_BULK_REMOVE_ABSOLUTE       0x0000000000000000ULL
#define   H_BULK_REMOVE_ANDCOND        0x0100000000000000ULL
#define   H_BULK_REMOVE_AVPN           0x0200000000000000ULL
#define H_BULK_REMOVE_PTEX             0x00ffffffffffffffULL

#define H_BULK_REMOVE_MAX_BATCH        4

static target_ulong h_bulk_remove(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                  target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    int i;
    target_ulong rc = H_SUCCESS;

    for (i = 0; i < H_BULK_REMOVE_MAX_BATCH; i++) {
        target_ulong *tsh = &args[i*2];
        target_ulong tsl = args[i*2 + 1];
        target_ulong v, r, ret;

        if ((*tsh & H_BULK_REMOVE_TYPE) == H_BULK_REMOVE_END) {
            break;
        } else if ((*tsh & H_BULK_REMOVE_TYPE) != H_BULK_REMOVE_REQUEST) {
            return H_PARAMETER;
        }

        *tsh &= H_BULK_REMOVE_PTEX | H_BULK_REMOVE_FLAGS;
        *tsh |= H_BULK_REMOVE_RESPONSE;

        if ((*tsh & H_BULK_REMOVE_ANDCOND) && (*tsh & H_BULK_REMOVE_AVPN)) {
            *tsh |= H_BULK_REMOVE_PARM;
            return H_PARAMETER;
        }

        ret = remove_hpte(cpu, *tsh & H_BULK_REMOVE_PTEX, tsl,
                          (*tsh & H_BULK_REMOVE_FLAGS) >> 26,
                          &v, &r);

        *tsh |= ret << 60;

        switch (ret) {
        case REMOVE_SUCCESS:
            *tsh |= (r & (HPTE64_R_C | HPTE64_R_R)) << 43;
            break;

        case REMOVE_PARM:
            rc = H_PARAMETER;
            goto exit;

        case REMOVE_HW:
            rc = H_HARDWARE;
            goto exit;
        }
    }
 exit:
    check_tlb_flush(env, true);

    return rc;
}

static target_ulong h_protect(PowerPCCPU *cpu, SpaprMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong avpn = args[2];
    const ppc_hash_pte64_t *hptes;
    target_ulong v, r;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, 1);
    v = ppc_hash64_hpte0(cpu, hptes, 0);
    r = ppc_hash64_hpte1(cpu, hptes, 0);
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn)) {
        return H_NOT_FOUND;
    }

    r &= ~(HPTE64_R_PP0 | HPTE64_R_PP | HPTE64_R_N |
           HPTE64_R_KEY_HI | HPTE64_R_KEY_LO);
    r |= (flags << 55) & HPTE64_R_PP0;
    r |= (flags << 48) & HPTE64_R_KEY_HI;
    r |= flags & (HPTE64_R_PP | HPTE64_R_N | HPTE64_R_KEY_LO);
    spapr_store_hpte(cpu, ptex,
                     (v & ~HPTE64_V_VALID) | HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, ptex, v, r);
    /* Flush the tlb */
    check_tlb_flush(env, true);
    /* Don't need a memory barrier, due to qemu's global lock */
    spapr_store_hpte(cpu, ptex, v | HPTE64_V_HPTE_DIRTY, r);
    return H_SUCCESS;
}

static target_ulong h_read(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    int i, ridx, n_entries = 1;
    const ppc_hash_pte64_t *hptes;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    if (flags & H_READ_4) {
        /* Clear the two low order bits */
        ptex &= ~(3ULL);
        n_entries = 4;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, n_entries);
    for (i = 0, ridx = 0; i < n_entries; i++) {
        args[ridx++] = ppc_hash64_hpte0(cpu, hptes, i);
        args[ridx++] = ppc_hash64_hpte1(cpu, hptes, i);
    }
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, n_entries);

    return H_SUCCESS;
}


static void hypercall_register_types(void)
{
    /* hcall-pft */
    spapr_register_hypercall(H_ENTER, h_enter);
    spapr_register_hypercall(H_REMOVE, h_remove);
    spapr_register_hypercall(H_PROTECT, h_protect);
    spapr_register_hypercall(H_READ, h_read);

    /* hcall-bulk */
    spapr_register_hypercall(H_BULK_REMOVE, h_bulk_remove);
}

type_init(hypercall_register_types)
