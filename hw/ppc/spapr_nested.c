#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "exec/exec-all.h"
#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/spapr_nested.h"
#include "cpu-models.h"
#include "mmu-book3s-v3.h"

#ifdef CONFIG_TCG
#define PRTS_MASK      0x1f

static target_ulong h_set_ptbl(PowerPCCPU *cpu,
                               SpaprMachineState *spapr,
                               target_ulong opcode,
                               target_ulong *args)
{
    target_ulong ptcr = args[0];

    if (!spapr_get_cap(spapr, SPAPR_CAP_NESTED_KVM_HV)) {
        return H_FUNCTION;
    }

    if ((ptcr & PRTS_MASK) + 12 - 4 > 12) {
        return H_PARAMETER;
    }

    spapr->nested.ptcr = ptcr; /* Save new partition table */

    return H_SUCCESS;
}

static target_ulong h_tlb_invalidate(PowerPCCPU *cpu,
                                     SpaprMachineState *spapr,
                                     target_ulong opcode,
                                     target_ulong *args)
{
    /*
     * The spapr virtual hypervisor nested HV implementation retains no L2
     * translation state except for TLB. And the TLB is always invalidated
     * across L1<->L2 transitions, so nothing is required here.
     */

    return H_SUCCESS;
}

static target_ulong h_copy_tofrom_guest(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    /*
     * This HCALL is not required, L1 KVM will take a slow path and walk the
     * page tables manually to do the data copy.
     */
    return H_FUNCTION;
}

static void nested_save_state(struct nested_ppc_state *save, PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    memcpy(save->gpr, env->gpr, sizeof(save->gpr));

    save->lr = env->lr;
    save->ctr = env->ctr;
    save->cfar = env->cfar;
    save->msr = env->msr;
    save->nip = env->nip;

    save->cr = ppc_get_cr(env);
    save->xer = cpu_read_xer(env);

    save->lpcr = env->spr[SPR_LPCR];
    save->lpidr = env->spr[SPR_LPIDR];
    save->pcr = env->spr[SPR_PCR];
    save->dpdes = env->spr[SPR_DPDES];
    save->hfscr = env->spr[SPR_HFSCR];
    save->srr0 = env->spr[SPR_SRR0];
    save->srr1 = env->spr[SPR_SRR1];
    save->sprg0 = env->spr[SPR_SPRG0];
    save->sprg1 = env->spr[SPR_SPRG1];
    save->sprg2 = env->spr[SPR_SPRG2];
    save->sprg3 = env->spr[SPR_SPRG3];
    save->pidr = env->spr[SPR_BOOKS_PID];
    save->ppr = env->spr[SPR_PPR];

    save->tb_offset = env->tb_env->tb_offset;
}

static void nested_load_state(PowerPCCPU *cpu, struct nested_ppc_state *load)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    memcpy(env->gpr, load->gpr, sizeof(env->gpr));

    env->lr = load->lr;
    env->ctr = load->ctr;
    env->cfar = load->cfar;
    env->msr = load->msr;
    env->nip = load->nip;

    ppc_set_cr(env, load->cr);
    cpu_write_xer(env, load->xer);

    env->spr[SPR_LPCR] = load->lpcr;
    env->spr[SPR_LPIDR] = load->lpidr;
    env->spr[SPR_PCR] = load->pcr;
    env->spr[SPR_DPDES] = load->dpdes;
    env->spr[SPR_HFSCR] = load->hfscr;
    env->spr[SPR_SRR0] = load->srr0;
    env->spr[SPR_SRR1] = load->srr1;
    env->spr[SPR_SPRG0] = load->sprg0;
    env->spr[SPR_SPRG1] = load->sprg1;
    env->spr[SPR_SPRG2] = load->sprg2;
    env->spr[SPR_SPRG3] = load->sprg3;
    env->spr[SPR_BOOKS_PID] = load->pidr;
    env->spr[SPR_PPR] = load->ppr;

    env->tb_env->tb_offset = load->tb_offset;

    /*
     * MSR updated, compute hflags and possible interrupts.
     */
    hreg_compute_hflags(env);
    ppc_maybe_interrupt(env);

    /*
     * Nested HV does not tag TLB entries between L1 and L2, so must
     * flush on transition.
     */
    tlb_flush(cs);
    env->reserve_addr = -1; /* Reset the reservation */
}

/*
 * When this handler returns, the environment is switched to the L2 guest
 * and TCG begins running that. spapr_exit_nested() performs the switch from
 * L2 back to L1 and returns from the H_ENTER_NESTED hcall.
 */
static target_ulong h_enter_nested(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   target_ulong opcode,
                                   target_ulong *args)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct nested_ppc_state l2_state;
    target_ulong hv_ptr = args[0];
    target_ulong regs_ptr = args[1];
    target_ulong hdec, now = cpu_ppc_load_tbl(env);
    target_ulong lpcr, lpcr_mask;
    struct kvmppc_hv_guest_state *hvstate;
    struct kvmppc_hv_guest_state hv_state;
    struct kvmppc_pt_regs *regs;
    hwaddr len;

    if (spapr->nested.ptcr == 0) {
        return H_NOT_AVAILABLE;
    }

    len = sizeof(*hvstate);
    hvstate = address_space_map(CPU(cpu)->as, hv_ptr, &len, false,
                                MEMTXATTRS_UNSPECIFIED);
    if (len != sizeof(*hvstate)) {
        address_space_unmap(CPU(cpu)->as, hvstate, len, 0, false);
        return H_PARAMETER;
    }

    memcpy(&hv_state, hvstate, len);

    address_space_unmap(CPU(cpu)->as, hvstate, len, len, false);

    /*
     * We accept versions 1 and 2. Version 2 fields are unused because TCG
     * does not implement DAWR*.
     */
    if (hv_state.version > HV_GUEST_STATE_VERSION) {
        return H_PARAMETER;
    }

    if (hv_state.lpid == 0) {
        return H_PARAMETER;
    }

    spapr_cpu->nested_host_state = g_try_new(struct nested_ppc_state, 1);
    if (!spapr_cpu->nested_host_state) {
        return H_NO_MEM;
    }

    assert(env->spr[SPR_LPIDR] == 0);
    assert(env->spr[SPR_DPDES] == 0);
    nested_save_state(spapr_cpu->nested_host_state, cpu);

    len = sizeof(*regs);
    regs = address_space_map(CPU(cpu)->as, regs_ptr, &len, false,
                                MEMTXATTRS_UNSPECIFIED);
    if (!regs || len != sizeof(*regs)) {
        address_space_unmap(CPU(cpu)->as, regs, len, 0, false);
        g_free(spapr_cpu->nested_host_state);
        return H_P2;
    }

    len = sizeof(l2_state.gpr);
    assert(len == sizeof(regs->gpr));
    memcpy(l2_state.gpr, regs->gpr, len);

    l2_state.lr = regs->link;
    l2_state.ctr = regs->ctr;
    l2_state.xer = regs->xer;
    l2_state.cr = regs->ccr;
    l2_state.msr = regs->msr;
    l2_state.nip = regs->nip;

    address_space_unmap(CPU(cpu)->as, regs, len, len, false);

    l2_state.cfar = hv_state.cfar;
    l2_state.lpidr = hv_state.lpid;

    lpcr_mask = LPCR_DPFD | LPCR_ILE | LPCR_AIL | LPCR_LD | LPCR_MER;
    lpcr = (env->spr[SPR_LPCR] & ~lpcr_mask) | (hv_state.lpcr & lpcr_mask);
    lpcr |= LPCR_HR | LPCR_UPRT | LPCR_GTSE | LPCR_HVICE | LPCR_HDICE;
    lpcr &= ~LPCR_LPES0;
    l2_state.lpcr = lpcr & pcc->lpcr_mask;

    l2_state.pcr = hv_state.pcr;
    /* hv_state.amor is not used */
    l2_state.dpdes = hv_state.dpdes;
    l2_state.hfscr = hv_state.hfscr;
    /* TCG does not implement DAWR*, CIABR, PURR, SPURR, IC, VTB, HEIR SPRs*/
    l2_state.srr0 = hv_state.srr0;
    l2_state.srr1 = hv_state.srr1;
    l2_state.sprg0 = hv_state.sprg[0];
    l2_state.sprg1 = hv_state.sprg[1];
    l2_state.sprg2 = hv_state.sprg[2];
    l2_state.sprg3 = hv_state.sprg[3];
    l2_state.pidr = hv_state.pidr;
    l2_state.ppr = hv_state.ppr;
    l2_state.tb_offset = env->tb_env->tb_offset + hv_state.tb_offset;

    /*
     * Switch to the nested guest environment and start the "hdec" timer.
     */
    nested_load_state(cpu, &l2_state);

    hdec = hv_state.hdec_expiry - now;
    cpu_ppc_hdecr_init(env);
    cpu_ppc_store_hdecr(env, hdec);

    /*
     * The hv_state.vcpu_token is not needed. It is used by the KVM
     * implementation to remember which L2 vCPU last ran on which physical
     * CPU so as to invalidate process scope translations if it is moved
     * between physical CPUs. For now TLBs are always flushed on L1<->L2
     * transitions so this is not a problem.
     *
     * Could validate that the same vcpu_token does not attempt to run on
     * different L1 vCPUs at the same time, but that would be a L1 KVM bug
     * and it's not obviously worth a new data structure to do it.
     */

    spapr_cpu->in_nested = true;

    /*
     * The spapr hcall helper sets env->gpr[3] to the return value, but at
     * this point the L1 is not returning from the hcall but rather we
     * start running the L2, so r3 must not be clobbered, so return env->gpr[3]
     * to leave it unchanged.
     */
    return env->gpr[3];
}

void spapr_exit_nested(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct nested_ppc_state l2_state;
    target_ulong hv_ptr = spapr_cpu->nested_host_state->gpr[4];
    target_ulong regs_ptr = spapr_cpu->nested_host_state->gpr[5];
    target_ulong hsrr0, hsrr1, hdar, asdr, hdsisr;
    struct kvmppc_hv_guest_state *hvstate;
    struct kvmppc_pt_regs *regs;
    hwaddr len;

    assert(spapr_cpu->in_nested);

    nested_save_state(&l2_state, cpu);
    hsrr0 = env->spr[SPR_HSRR0];
    hsrr1 = env->spr[SPR_HSRR1];
    hdar = env->spr[SPR_HDAR];
    hdsisr = env->spr[SPR_HDSISR];
    asdr = env->spr[SPR_ASDR];

    /*
     * Switch back to the host environment (including for any error).
     */
    assert(env->spr[SPR_LPIDR] != 0);
    nested_load_state(cpu, spapr_cpu->nested_host_state);
    env->gpr[3] = env->excp_vectors[excp]; /* hcall return value */

    cpu_ppc_hdecr_exit(env);

    spapr_cpu->in_nested = false;

    g_free(spapr_cpu->nested_host_state);
    spapr_cpu->nested_host_state = NULL;

    len = sizeof(*hvstate);
    hvstate = address_space_map(CPU(cpu)->as, hv_ptr, &len, true,
                                MEMTXATTRS_UNSPECIFIED);
    if (len != sizeof(*hvstate)) {
        address_space_unmap(CPU(cpu)->as, hvstate, len, 0, true);
        env->gpr[3] = H_PARAMETER;
        return;
    }

    hvstate->cfar = l2_state.cfar;
    hvstate->lpcr = l2_state.lpcr;
    hvstate->pcr = l2_state.pcr;
    hvstate->dpdes = l2_state.dpdes;
    hvstate->hfscr = l2_state.hfscr;

    if (excp == POWERPC_EXCP_HDSI) {
        hvstate->hdar = hdar;
        hvstate->hdsisr = hdsisr;
        hvstate->asdr = asdr;
    } else if (excp == POWERPC_EXCP_HISI) {
        hvstate->asdr = asdr;
    }

    /* HEIR should be implemented for HV mode and saved here. */
    hvstate->srr0 = l2_state.srr0;
    hvstate->srr1 = l2_state.srr1;
    hvstate->sprg[0] = l2_state.sprg0;
    hvstate->sprg[1] = l2_state.sprg1;
    hvstate->sprg[2] = l2_state.sprg2;
    hvstate->sprg[3] = l2_state.sprg3;
    hvstate->pidr = l2_state.pidr;
    hvstate->ppr = l2_state.ppr;

    /* Is it okay to specify write length larger than actual data written? */
    address_space_unmap(CPU(cpu)->as, hvstate, len, len, true);

    len = sizeof(*regs);
    regs = address_space_map(CPU(cpu)->as, regs_ptr, &len, true,
                                MEMTXATTRS_UNSPECIFIED);
    if (!regs || len != sizeof(*regs)) {
        address_space_unmap(CPU(cpu)->as, regs, len, 0, true);
        env->gpr[3] = H_P2;
        return;
    }

    len = sizeof(env->gpr);
    assert(len == sizeof(regs->gpr));
    memcpy(regs->gpr, l2_state.gpr, len);

    regs->link = l2_state.lr;
    regs->ctr = l2_state.ctr;
    regs->xer = l2_state.xer;
    regs->ccr = l2_state.cr;

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_SYSCALL) {
        regs->nip = l2_state.srr0;
        regs->msr = l2_state.srr1 & env->msr_mask;
    } else {
        regs->nip = hsrr0;
        regs->msr = hsrr1 & env->msr_mask;
    }

    /* Is it okay to specify write length larger than actual data written? */
    address_space_unmap(CPU(cpu)->as, regs, len, len, true);
}

SpaprMachineStateNestedGuest *spapr_get_nested_guest(SpaprMachineState *spapr,
                                                     target_ulong lpid)
{
    SpaprMachineStateNestedGuest *guest;

    guest = g_hash_table_lookup(spapr->nested.guests, GINT_TO_POINTER(lpid));
    return guest;
}

static bool vcpu_check(SpaprMachineStateNestedGuest *guest,
                       target_ulong vcpuid,
                       bool inoutbuf)
{
    struct SpaprMachineStateNestedGuestVcpu *vcpu;

    if (vcpuid >= NESTED_GUEST_VCPU_MAX) {
        return false;
    }

    if (!(vcpuid < guest->vcpus)) {
        return false;
    }

    vcpu = &guest->vcpu[vcpuid];
    if (!vcpu->enabled) {
        return false;
    }

    if (!inoutbuf) {
        return true;
    }

    /* Check to see if the in/out buffers are registered */
    if (vcpu->runbufin.addr && vcpu->runbufout.addr) {
        return true;
    }

    return false;
}

static void *get_vcpu_env_ptr(SpaprMachineStateNestedGuest *guest,
                              target_ulong vcpuid)
{
    assert(vcpu_check(guest, vcpuid, false));
    return &guest->vcpu[vcpuid].env;
}

static void *get_vcpu_ptr(SpaprMachineStateNestedGuest *guest,
                                   target_ulong vcpuid)
{
    assert(vcpu_check(guest, vcpuid, false));
    return &guest->vcpu[vcpuid];
}

static void *get_guest_ptr(SpaprMachineStateNestedGuest *guest,
                           target_ulong vcpuid)
{
    return guest;
}

/*
 * set=1 means the L1 is trying to set some state
 * set=0 means the L1 is trying to get some state
 */
static void copy_state_8to8(void *a, void *b, bool set)
{
    /* set takes from the Big endian element_buf and sets internal buffer */

    if (set) {
        *(uint64_t *)a = be64_to_cpu(*(uint64_t *)b);
    } else {
        *(uint64_t *)b = cpu_to_be64(*(uint64_t *)a);
    }
}

static void copy_state_16to16(void *a, void *b, bool set)
{
    uint64_t *src, *dst;

    if (set) {
        src = b;
        dst = a;

        dst[1] = be64_to_cpu(src[0]);
        dst[0] = be64_to_cpu(src[1]);
    } else {
        src = a;
        dst = b;

        dst[1] = cpu_to_be64(src[0]);
        dst[0] = cpu_to_be64(src[1]);
    }
}

static void copy_state_4to8(void *a, void *b, bool set)
{
    if (set) {
        *(uint64_t *)a  = (uint64_t) be32_to_cpu(*(uint32_t *)b);
    } else {
        *(uint32_t *)b = cpu_to_be32((uint32_t) (*((uint64_t *)a)));
    }
}

static void copy_state_pagetbl(void *a, void *b, bool set)
{
    uint64_t *pagetbl;
    uint64_t *buf; /* 3 double words */
    uint64_t rts;

    assert(set);

    pagetbl = a;
    buf = b;

    *pagetbl = be64_to_cpu(buf[0]);
    /* as per ISA section 6.7.6.1 */
    *pagetbl |= PATE0_HR; /* Host Radix bit is 1 */

    /* RTS */
    rts = be64_to_cpu(buf[1]);
    assert(rts == 52);
    rts = rts - 31; /* since radix tree size = 2^(RTS+31) */
    *pagetbl |=  ((rts & 0x7) << 5); /* RTS2 is bit 56:58 */
    *pagetbl |=  (((rts >> 3) & 0x3) << 61); /* RTS1 is bit 1:2 */

    /* RPDS {Size = 2^(RPDS+3) , RPDS >=5} */
    *pagetbl |= 63 - clz64(be64_to_cpu(buf[2])) - 3;
}

static void copy_state_proctbl(void *a, void *b, bool set)
{
    uint64_t *proctbl;
    uint64_t *buf; /* 2 double words */

    assert(set);

    proctbl = a;
    buf = b;
    /* PRTB: Process Table Base */
    *proctbl = be64_to_cpu(buf[0]);
    /* PRTS: Process Table Size = 2^(12+PRTS) */
    if (be64_to_cpu(buf[1]) == (1ULL << 12)) {
            *proctbl |= 0;
    } else if (be64_to_cpu(buf[1]) == (1ULL << 24)) {
            *proctbl |= 12;
    } else {
        g_assert_not_reached();
    }
}

static void copy_state_runbuf(void *a, void *b, bool set)
{
    uint64_t *buf; /* 2 double words */
    struct SpaprMachineStateNestedGuestVcpuRunBuf *runbuf;

    assert(set);

    runbuf = a;
    buf = b;

    runbuf->addr = be64_to_cpu(buf[0]);
    assert(runbuf->addr);

    /* per spec */
    assert(be64_to_cpu(buf[1]) <= 16384);

    /*
     * This will also hit in the input buffer but should be fine for
     * now. If not we can split this function.
     */
    assert(be64_to_cpu(buf[1]) >= VCPU_OUT_BUF_MIN_SZ);

    runbuf->size = be64_to_cpu(buf[1]);
}

/* tell the L1 how big we want the output vcpu run buffer */
static void out_buf_min_size(void *a, void *b, bool set)
{
    uint64_t *buf; /* 1 double word */

    assert(!set);

    buf = b;

    buf[0] = cpu_to_be64(VCPU_OUT_BUF_MIN_SZ);
}

static void copy_logical_pvr(void *a, void *b, bool set)
{
    uint32_t *buf; /* 1 word */
    uint32_t *pvr_logical_ptr;
    uint32_t pvr_logical;

    pvr_logical_ptr = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be32(*pvr_logical_ptr);
        return;
    }

    pvr_logical = be32_to_cpu(buf[0]);
    /* don't change the major version */
    assert((pvr_logical & CPU_POWERPC_POWER_SERVER_MASK) ==
           (*pvr_logical_ptr & CPU_POWERPC_POWER_SERVER_MASK));

    *pvr_logical_ptr = pvr_logical;
}

static void copy_tb_offset(void *a, void *b, bool set)
{
    SpaprMachineStateNestedGuest *guest;
    uint64_t *buf; /* 1 double word */
    uint64_t *tb_offset_ptr;
    uint64_t tb_offset;

    tb_offset_ptr = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(*tb_offset_ptr);
        return;
    }

    tb_offset = be64_to_cpu(buf[0]);
    /* need to copy this to the individual tb_offset for each vcpu */
    guest = container_of(tb_offset_ptr,
                         struct SpaprMachineStateNestedGuest,
                         tb_offset);
    for (int i = 0; i < guest->vcpus; i++) {
        guest->vcpu[i].tb_offset = tb_offset;
    }
}

static void copy_state_dec_expire_tb(void *a, void *b, bool set)
{
    int64_t *dec_expiry_tb;
    uint64_t *buf; /* 1 double word */

    dec_expiry_tb = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(*dec_expiry_tb);
        return;
    }

    *dec_expiry_tb = be64_to_cpu(buf[0]);
}

static void copy_state_hdecr(void *a, void *b, bool set)
{
    uint64_t *buf; /* 1 double word */
    CPUPPCState *env;

    env = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(env->tb_env->hdecr_expiry_tb);
        return;
    }

    env->tb_env->hdecr_expiry_tb = be64_to_cpu(buf[0]);
}

static void copy_state_vscr(void *a, void *b, bool set)
{
    uint32_t *buf; /* 1 word */
    CPUPPCState *env;

    env = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be32(ppc_get_vscr(env));
        return;
    }

    ppc_store_vscr(env, be32_to_cpu(buf[0]));
}

static void copy_state_fpscr(void *a, void *b, bool set)
{
    uint64_t *buf; /* 1 double word */
    CPUPPCState *env;

    env = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(env->fpscr);
        return;
    }

    ppc_store_fpscr(env, be64_to_cpu(buf[0]));
}

static void copy_state_cr(void *a, void *b, bool set)
{
    uint32_t *buf; /* 1 word */
    CPUPPCState *env;
    uint64_t cr; /* api v1 uses uint64_t but papr acr v2 mentions 4 bytes */
    env = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be32((uint32_t)ppc_get_cr(env));
        return;
    }
    cr = be32_to_cpu(buf[0]);
    ppc_set_cr(env, cr);
}

struct guest_state_element_type guest_state_element_types[] = {
    GUEST_STATE_ELEMENT_NOP(GSB_HV_VCPU_IGNORED_ID, 0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR0,  gpr[0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR1,  gpr[1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR2,  gpr[2]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR3,  gpr[3]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR4,  gpr[4]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR5,  gpr[5]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR6,  gpr[6]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR7,  gpr[7]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR8,  gpr[8]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR9,  gpr[9]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR10, gpr[10]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR11, gpr[11]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR12, gpr[12]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR13, gpr[13]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR14, gpr[14]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR15, gpr[15]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR16, gpr[16]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR17, gpr[17]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR18, gpr[18]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR19, gpr[19]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR20, gpr[20]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR21, gpr[21]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR22, gpr[22]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR23, gpr[23]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR24, gpr[24]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR25, gpr[25]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR26, gpr[26]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR27, gpr[27]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR28, gpr[28]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR29, gpr[29]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR30, gpr[30]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR31, gpr[31]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_NIA, nip),
    GSE_ENV_DWM(GSB_VCPU_SPR_MSR, msr, HVMASK_MSR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CTR, ctr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_LR, lr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_XER, xer),
    GUEST_STATE_ELEMENT_ENV_BASE(GSB_VCPU_SPR_CR, 4, copy_state_cr),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_MMCR3),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_SIER2),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_SIER3),
    GUEST_STATE_ELEMENT_NOP_W(GSB_VCPU_SPR_WORT),
    GSE_ENV_DWM(GSB_VCPU_SPR_LPCR, spr[SPR_LPCR], HVMASK_LPCR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_AMOR, spr[SPR_AMOR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HFSCR, spr[SPR_HFSCR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAWR0, spr[SPR_DAWR0]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DAWRX0, spr[SPR_DAWRX0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CIABR, spr[SPR_CIABR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_PURR,  spr[SPR_PURR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPURR, spr[SPR_SPURR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_IC,    spr[SPR_IC]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_VTB,   spr[SPR_VTB]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HDAR,  spr[SPR_HDAR]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_HDSISR, spr[SPR_HDSISR]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_HEIR,   spr[SPR_HEIR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_ASDR,  spr[SPR_ASDR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SRR0, spr[SPR_SRR0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SRR1, spr[SPR_SRR1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG0, spr[SPR_SPRG0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG1, spr[SPR_SPRG1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG2, spr[SPR_SPRG2]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG3, spr[SPR_SPRG3]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PIDR,   spr[SPR_BOOKS_PID]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CFAR, cfar),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_PPR, spr[SPR_PPR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAWR1, spr[SPR_DAWR1]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DAWRX1, spr[SPR_DAWRX1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DEXCR, spr[SPR_DEXCR]),
    GSE_ENV_DWM(GSB_VCPU_SPR_HDEXCR, spr[SPR_HDEXCR], HVMASK_HDEXCR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HASHKEYR,  spr[SPR_HASHKEYR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HASHPKEYR, spr[SPR_HASHPKEYR]),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR0, 16, vsr[0], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR1, 16, vsr[1], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR2, 16, vsr[2], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR3, 16, vsr[3], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR4, 16, vsr[4], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR5, 16, vsr[5], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR6, 16, vsr[6], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR7, 16, vsr[7], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR8, 16, vsr[8], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR9, 16, vsr[9], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR10, 16, vsr[10], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR11, 16, vsr[11], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR12, 16, vsr[12], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR13, 16, vsr[13], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR14, 16, vsr[14], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR15, 16, vsr[15], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR16, 16, vsr[16], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR17, 16, vsr[17], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR18, 16, vsr[18], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR19, 16, vsr[19], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR20, 16, vsr[20], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR21, 16, vsr[21], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR22, 16, vsr[22], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR23, 16, vsr[23], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR24, 16, vsr[24], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR25, 16, vsr[25], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR26, 16, vsr[26], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR27, 16, vsr[27], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR28, 16, vsr[28], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR29, 16, vsr[29], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR30, 16, vsr[30], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR31, 16, vsr[31], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR32, 16, vsr[32], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR33, 16, vsr[33], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR34, 16, vsr[34], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR35, 16, vsr[35], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR36, 16, vsr[36], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR37, 16, vsr[37], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR38, 16, vsr[38], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR39, 16, vsr[39], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR40, 16, vsr[40], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR41, 16, vsr[41], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR42, 16, vsr[42], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR43, 16, vsr[43], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR44, 16, vsr[44], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR45, 16, vsr[45], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR46, 16, vsr[46], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR47, 16, vsr[47], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR48, 16, vsr[48], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR49, 16, vsr[49], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR50, 16, vsr[50], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR51, 16, vsr[51], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR52, 16, vsr[52], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR53, 16, vsr[53], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR54, 16, vsr[54], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR55, 16, vsr[55], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR56, 16, vsr[56], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR57, 16, vsr[57], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR58, 16, vsr[58], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR59, 16, vsr[59], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR60, 16, vsr[60], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR61, 16, vsr[61], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR62, 16, vsr[62], copy_state_16to16),
    GUEST_STATE_ELEMENT_ENV(GSB_VCPU_SPR_VSR63, 16, vsr[63], copy_state_16to16),
    GSBE_NESTED(GSB_PART_SCOPED_PAGETBL, 0x18, parttbl[0],  copy_state_pagetbl),
    GSBE_NESTED(GSB_PROCESS_TBL,         0x10, parttbl[1],  copy_state_proctbl),
    GSBE_NESTED(GSB_VCPU_LPVR,           0x4,  pvr_logical, copy_logical_pvr),
    GSBE_NESTED_MSK(GSB_TB_OFFSET, 0x8, tb_offset, copy_tb_offset,
                    HVMASK_TB_OFFSET),
    GSBE_NESTED_VCPU(GSB_VCPU_IN_BUFFER, 0x10, runbufin,    copy_state_runbuf),
    GSBE_NESTED_VCPU(GSB_VCPU_OUT_BUFFER, 0x10, runbufout,   copy_state_runbuf),
    GSBE_NESTED_VCPU(GSB_VCPU_OUT_BUF_MIN_SZ, 0x8, runbufout, out_buf_min_size),
    GSBE_NESTED_VCPU(GSB_VCPU_DEC_EXPIRE_TB, 0x8, dec_expiry_tb,
                     copy_state_dec_expire_tb),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_EBBHR, spr[SPR_EBBHR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_TAR,   spr[SPR_TAR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_EBBRR, spr[SPR_EBBRR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_BESCR, spr[SPR_BESCR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_IAMR , spr[SPR_IAMR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_AMR  , spr[SPR_AMR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_UAMOR, spr[SPR_UAMOR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DSCR , spr[SPR_DSCR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_FSCR , spr[SPR_FSCR]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PSPB , spr[SPR_PSPB]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CTRL , spr[SPR_CTRL]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_VRSAVE, spr[SPR_VRSAVE]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAR , spr[SPR_DAR]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DSISR , spr[SPR_DSISR]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC1, spr[SPR_POWER_PMC1]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC2, spr[SPR_POWER_PMC2]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC3, spr[SPR_POWER_PMC3]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC4, spr[SPR_POWER_PMC4]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC5, spr[SPR_POWER_PMC5]),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC6, spr[SPR_POWER_PMC6]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR0, spr[SPR_POWER_MMCR0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR1, spr[SPR_POWER_MMCR1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR2, spr[SPR_POWER_MMCR2]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCRA, spr[SPR_POWER_MMCRA]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SDAR , spr[SPR_POWER_SDAR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SIAR , spr[SPR_POWER_SIAR]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SIER , spr[SPR_POWER_SIER]),
    GUEST_STATE_ELEMENT_ENV_BASE(GSB_VCPU_HDEC_EXPIRY_TB, 8, copy_state_hdecr),
    GUEST_STATE_ELEMENT_ENV_BASE(GSB_VCPU_SPR_VSCR,  4, copy_state_vscr),
    GUEST_STATE_ELEMENT_ENV_BASE(GSB_VCPU_SPR_FPSCR, 8, copy_state_fpscr)
};

void init_nested(void)
{
    struct guest_state_element_type *type;
    int i;

    /* Init the guest state elements lookup table, flags for now */
    for (i = 0; i < ARRAY_SIZE(guest_state_element_types); i++) {
        type = &guest_state_element_types[i];

        assert(type->id <= GSB_LAST);
        if (type->id >= GSB_VCPU_SPR_HDAR)
            /* 0xf000 - 0xf005 Thread + RO */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY;
        else if (type->id >= GSB_VCPU_IN_BUFFER)
            /* 0x0c00 - 0xf000 Thread + RW */
            type->flags = 0;
        else if (type->id >= GSB_VCPU_LPVR)
            /* 0x0003 - 0x0bff Guest + RW */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE;
        else if (type->id >= GSB_HV_VCPU_STATE_SIZE)
            /* 0x0001 - 0x0002 Guest + RO */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY |
                          GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE;
    }
}

static struct guest_state_element *guest_state_element_next(
    struct guest_state_element *element,
    int64_t *len,
    int64_t *num_elements)
{
    uint16_t size;

    /* size is of element->value[] only. Not whole guest_state_element */
    size = be16_to_cpu(element->size);

    if (len) {
        *len -= size + offsetof(struct guest_state_element, value);
    }

    if (num_elements) {
        *num_elements -= 1;
    }

    return (struct guest_state_element *)(element->value + size);
}

static
struct guest_state_element_type *guest_state_element_type_find(uint16_t id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(guest_state_element_types); i++)
        if (id == guest_state_element_types[i].id) {
            return &guest_state_element_types[i];
        }

    return NULL;
}

static void print_element(struct guest_state_element *element,
                          struct guest_state_request *gsr)
{
    printf("id:0x%04x size:0x%04x %s ",
           be16_to_cpu(element->id), be16_to_cpu(element->size),
           gsr->flags & GUEST_STATE_REQUEST_SET ? "set" : "get");
    printf("buf:0x%016lx ...\n", be64_to_cpu(*(uint64_t *)element->value));
}

static bool guest_state_request_check(struct guest_state_request *gsr)
{
    int64_t num_elements, len = gsr->len;
    struct guest_state_buffer *gsb = gsr->gsb;
    struct guest_state_element *element;
    struct guest_state_element_type *type;
    uint16_t id, size;

    /* gsb->num_elements = 0 == 32 bits long */
    assert(len >= 4);

    num_elements = be32_to_cpu(gsb->num_elements);
    element = gsb->elements;
    len -= sizeof(gsb->num_elements);

    /* Walk the buffer to validate the length */
    while (num_elements) {

        id = be16_to_cpu(element->id);
        size = be16_to_cpu(element->size);

        if (false) {
            print_element(element, gsr);
        }
        /* buffer size too small */
        if (len < 0) {
            return false;
        }

        type = guest_state_element_type_find(id);
        if (!type) {
            printf("%s: Element ID %04x unknown\n", __func__, id);
            print_element(element, gsr);
            return false;
        }

        if (id == GSB_HV_VCPU_IGNORED_ID) {
            goto next_element;
        }

        if (size != type->size) {
            printf("%s: Size mismatch. Element ID:%04x. Size Exp:%i Got:%i\n",
                   __func__, id, type->size, size);
            print_element(element, gsr);
            return false;
        }

        if ((type->flags & GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY) &&
            (gsr->flags & GUEST_STATE_REQUEST_SET)) {
            printf("%s: trying to set a read-only Element ID:%04x.\n",
                   __func__, id);
            return false;
        }

        if (type->flags & GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE) {
            /* guest wide element type */
            if (!(gsr->flags & GUEST_STATE_REQUEST_GUEST_WIDE)) {
                printf("%s: trying to set a guest wide Element ID:%04x.\n",
                       __func__, id);
                return false;
            }
        } else {
            /* thread wide element type */
            if (gsr->flags & GUEST_STATE_REQUEST_GUEST_WIDE) {
                printf("%s: trying to set a thread wide Element ID:%04x.\n",
                       __func__, id);
                return false;
            }
        }
next_element:
        element = guest_state_element_next(element, &len, &num_elements);

    }
    return true;
}

static bool is_gsr_invalid(struct guest_state_request *gsr,
                                   struct guest_state_element *element,
                                   struct guest_state_element_type *type)
{
    if ((gsr->flags & GUEST_STATE_REQUEST_SET) &&
        (*(uint64_t *)(element->value) & ~(type->mask))) {
        print_element(element, gsr);
        printf("L1 can't set reserved bits (allowed mask: 0x%08lx)\n",
               type->mask);
        return true;
    }
    return false;
}

static target_ulong h_guest_get_capabilities(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];

    if (flags) { /* don't handle any flags capabilities for now */
        return H_PARAMETER;
    }

    if ((env->spr[SPR_PVR] & CPU_POWERPC_POWER_SERVER_MASK) ==
        (CPU_POWERPC_POWER9_BASE))
        env->gpr[4] = H_GUEST_CAPABILITIES_P9_MODE;

    if ((env->spr[SPR_PVR] & CPU_POWERPC_POWER_SERVER_MASK) ==
        (CPU_POWERPC_POWER10_BASE))
        env->gpr[4] = H_GUEST_CAPABILITIES_P10_MODE;

    return H_SUCCESS;
}

static target_ulong h_guest_set_capabilities(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                              target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong capabilities = args[1];

    if (flags) { /* don't handle any flags capabilities for now */
        return H_PARAMETER;
    }


    /* isn't supported */
    if (capabilities & H_GUEST_CAPABILITIES_COPY_MEM) {
        env->gpr[4] = 0;
        return H_P2;
    }

    if ((env->spr[SPR_PVR] & CPU_POWERPC_POWER_SERVER_MASK) ==
        (CPU_POWERPC_POWER9_BASE)) {
        /* We are a P9 */
        if (!(capabilities & H_GUEST_CAPABILITIES_P9_MODE)) {
            env->gpr[4] = 1;
            return H_P2;
        }
    }

    if ((env->spr[SPR_PVR] & CPU_POWERPC_POWER_SERVER_MASK) ==
        (CPU_POWERPC_POWER10_BASE)) {
        /* We are a P10 */
        if (!(capabilities & H_GUEST_CAPABILITIES_P10_MODE)) {
            env->gpr[4] = 2;
            return H_P2;
        }
    }

    spapr->nested.capabilities_set = true;

    spapr->nested.pvr_base = env->spr[SPR_PVR];

    return H_SUCCESS;
}

static void
destroy_guest_helper(gpointer value)
{
    struct SpaprMachineStateNestedGuest *guest = value;
    int i = 0;
    for (i = 0; i < guest->vcpus; i++) {
        cpu_ppc_tb_free(&guest->vcpu[i].env);
    }
    g_free(guest->vcpu);
    g_free(guest);
}

static target_ulong h_guest_create(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   target_ulong opcode,
                                   target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong continue_token = args[1];
    uint64_t lpid;
    int nguests = 0;
    struct SpaprMachineStateNestedGuest *guest;

    if (flags) { /* don't handle any flags for now */
        return H_UNSUPPORTED_FLAG;
    }

    if (continue_token != -1) {
        return H_P2;
    }

    if (!spapr_get_cap(spapr, SPAPR_CAP_NESTED_PAPR)) {
        return H_FUNCTION;
    }

    if (!spapr->nested.capabilities_set) {
        return H_STATE;
    }

    if (!spapr->nested.guests) {
        spapr->nested.lpid_max = NESTED_GUEST_MAX;
        spapr->nested.guests = g_hash_table_new_full(NULL,
                                                     NULL,
                                                     NULL,
                                                     destroy_guest_helper);
    }

    nguests = g_hash_table_size(spapr->nested.guests);

    if (nguests == spapr->nested.lpid_max) {
        return H_NO_MEM;
    }

    /* Lookup for available lpid */
    for (lpid = 1; lpid < spapr->nested.lpid_max; lpid++) {
        if (!(g_hash_table_lookup(spapr->nested.guests,
                                  GINT_TO_POINTER(lpid)))) {
            break;
        }
    }
    if (lpid == spapr->nested.lpid_max) {
        return H_NO_MEM;
    }

    guest = g_try_new0(struct SpaprMachineStateNestedGuest, 1);
    if (!guest) {
        return H_NO_MEM;
    }

    guest->pvr_logical = spapr->nested.pvr_base;

    g_hash_table_insert(spapr->nested.guests, GINT_TO_POINTER(lpid), guest);
    printf("%s: lpid: %lu (MAX: %i)\n", __func__, lpid, spapr->nested.lpid_max);

    env->gpr[4] = lpid;
    return H_SUCCESS;
}

static target_ulong h_guest_create_vcpu(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    CPUPPCState *env = &cpu->env, *l2env;
    target_ulong flags = args[0];
    target_ulong lpid = args[1];
    target_ulong vcpuid = args[2];
    SpaprMachineStateNestedGuest *guest;

    if (flags) { /* don't handle any flags for now */
        return H_UNSUPPORTED_FLAG;
    }

    guest = spapr_get_nested_guest(spapr, lpid);
    if (!guest) {
        return H_P2;
    }

    if (vcpuid < guest->vcpus) {
        return H_IN_USE;
    }

    if (guest->vcpus >= NESTED_GUEST_VCPU_MAX) {
        return H_P3;
    }

    if (guest->vcpus) {
        struct SpaprMachineStateNestedGuestVcpu *vcpus;
        vcpus = g_try_renew(struct SpaprMachineStateNestedGuestVcpu,
                            guest->vcpu,
                            guest->vcpus + 1);
        if (!vcpus) {
            return H_NO_MEM;
        }
        memset(&vcpus[guest->vcpus], 0,
               sizeof(struct SpaprMachineStateNestedGuestVcpu));
        guest->vcpu = vcpus;
        l2env = &vcpus[guest->vcpus].env;
    } else {
        guest->vcpu = g_try_new0(struct SpaprMachineStateNestedGuestVcpu, 1);
        if (guest->vcpu == NULL) {
            return H_NO_MEM;
        }
        l2env = &guest->vcpu->env;
    }
    /* need to memset to zero otherwise we leak L1 state to L2 */
    memset(l2env, 0, sizeof(CPUPPCState));
    /* Copy L1 PVR to L2 */
    l2env->spr[SPR_PVR] = env->spr[SPR_PVR];
    cpu_ppc_tb_init(l2env, SPAPR_TIMEBASE_FREQ);

    guest->vcpus++;
    assert(vcpuid < guest->vcpus); /* linear vcpuid allocation only */
    guest->vcpu[vcpuid].enabled = true;

    if (!vcpu_check(guest, vcpuid, false)) {
        return H_PARAMETER;
    }
    return H_SUCCESS;
}

static target_ulong getset_state(SpaprMachineStateNestedGuest *guest,
                                 uint64_t vcpuid,
                                 struct guest_state_request *gsr)
{
    void *ptr;
    uint16_t id;
    struct guest_state_element *element;
    struct guest_state_element_type *type;
    int64_t lenleft, num_elements;

    lenleft = gsr->len;

    if (!guest_state_request_check(gsr)) {
        return H_P3;
    }

    num_elements = be32_to_cpu(gsr->gsb->num_elements);
    element = gsr->gsb->elements;
    /* Process the elements */
    while (num_elements) {
        type = NULL;
        /* Debug print before doing anything */
        if (false) {
            print_element(element, gsr);
        }

        id = be16_to_cpu(element->id);
        if (id == GSB_HV_VCPU_IGNORED_ID) {
            goto next_element;
        }

        type = guest_state_element_type_find(id);
        assert(type);

        /* Get pointer to guest data to get/set */
        if (type->location && type->copy) {
            ptr = type->location(guest, vcpuid);
            assert(ptr);
            if (!~(type->mask) && is_gsr_invalid(gsr, element, type)) {
                return H_INVALID_ELEMENT_VALUE;
            }
            type->copy(ptr + type->offset, element->value,
                       gsr->flags & GUEST_STATE_REQUEST_SET ? true : false);
        }

next_element:
        element = guest_state_element_next(element, &lenleft, &num_elements);
    }

    return H_SUCCESS;
}

static target_ulong map_and_getset_state(PowerPCCPU *cpu,
                                         SpaprMachineStateNestedGuest *guest,
                                         uint64_t vcpuid,
                                         struct guest_state_request *gsr)
{
    target_ulong rc;
    int64_t lenleft, len;
    bool is_write;

    assert(gsr->len < (1024 * 1024)); /* sanity check */

    lenleft = len = gsr->len;
    gsr->gsb = address_space_map(CPU(cpu)->as, gsr->buf, (uint64_t *)&len,
                                 false, MEMTXATTRS_UNSPECIFIED);
    if (!gsr->gsb) {
        rc = H_P3;
        goto out1;
    }

    if (len != lenleft) {
        rc = H_P3;
        goto out1;
    }

    rc = getset_state(guest, vcpuid, gsr);

out1:
    is_write = (rc == H_SUCCESS) ? len : 0;
    address_space_unmap(CPU(cpu)->as, gsr->gsb, len, is_write, false);
    return rc;
}

static target_ulong h_guest_getset_state(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong *args,
                                         bool set)
{
    target_ulong flags = args[0];
    target_ulong lpid = args[1];
    target_ulong vcpuid = args[2];
    target_ulong buf = args[3];
    target_ulong buflen = args[4];
    struct guest_state_request gsr;
    SpaprMachineStateNestedGuest *guest;

    guest = spapr_get_nested_guest(spapr, lpid);
    if (!guest) {
        return H_P2;
    }
    gsr.buf = buf;
    gsr.len = buflen;
    gsr.flags = 0;
    if (flags & H_GUEST_GETSET_STATE_FLAG_GUEST_WIDE) {
        gsr.flags |= GUEST_STATE_REQUEST_GUEST_WIDE;
    }
    if (flags & !H_GUEST_GETSET_STATE_FLAG_GUEST_WIDE) {
        return H_PARAMETER; /* flag not supported yet */
    }

    if (set) {
        gsr.flags |= GUEST_STATE_REQUEST_SET;
    }
    return map_and_getset_state(cpu, guest, vcpuid, &gsr);
}

static target_ulong h_guest_set_state(PowerPCCPU *cpu,
                                      SpaprMachineState *spapr,
                                      target_ulong opcode,
                                      target_ulong *args)
{
    return h_guest_getset_state(cpu, spapr, args, true);
}

static target_ulong h_guest_get_state(PowerPCCPU *cpu,
                                      SpaprMachineState *spapr,
                                      target_ulong opcode,
                                      target_ulong *args)
{
    return h_guest_getset_state(cpu, spapr, args, false);
}

void spapr_register_nested(void)
{
    spapr_register_hypercall(KVMPPC_H_SET_PARTITION_TABLE, h_set_ptbl);
    spapr_register_hypercall(KVMPPC_H_ENTER_NESTED, h_enter_nested);
    spapr_register_hypercall(KVMPPC_H_TLB_INVALIDATE, h_tlb_invalidate);
    spapr_register_hypercall(KVMPPC_H_COPY_TOFROM_GUEST, h_copy_tofrom_guest);
}

void spapr_register_nested_phyp(void)
{
    spapr_register_hypercall(H_GUEST_GET_CAPABILITIES, h_guest_get_capabilities);
    spapr_register_hypercall(H_GUEST_SET_CAPABILITIES, h_guest_set_capabilities);
    spapr_register_hypercall(H_GUEST_CREATE          , h_guest_create);
    spapr_register_hypercall(H_GUEST_CREATE_VCPU     , h_guest_create_vcpu);
    spapr_register_hypercall(H_GUEST_SET_STATE       , h_guest_set_state);
    spapr_register_hypercall(H_GUEST_GET_STATE       , h_guest_get_state);
}

#else
void spapr_exit_nested(PowerPCCPU *cpu, int excp)
{
    g_assert_not_reached();
}

void spapr_register_nested(void)
{
    /* DO NOTHING */
}

void spapr_register_nested_phyp(void)
{
    /* DO NOTHING */
}

void init_nested(void)
{
    /* DO NOTHING */
}

#endif
