/*
 * windbgstub-utils.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "exec/windbgstub-utils.h"

#ifdef TARGET_X86_64
# define OFFSET_SELF_PCR         0x18
# define OFFSET_VERS             0x108
# define OFFSET_KPRCB            0x20
# define OFFSET_KPRCB_CURRTHREAD 0x8
#else
# define OFFSET_SELF_PCR         0x1C
# define OFFSET_VERS             0x34
# define OFFSET_KPRCB            0x20
# define OFFSET_KPRCB_CURRTHREAD 0x4
#endif

typedef struct KDData {
    InitedAddr KPCR;
    InitedAddr version;
} KDData;

static KDData *kd;

void kd_api_unsupported(CPUState *cpu, PacketData *pd)
{
    WINDBG_ERROR("Catched unimplemented api %s",
                 KD_API_NAME(pd->m64.ApiNumber));
    pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    pd->extra_size = 0;

    exit(1);
}

static void kd_breakpoint_remove_range(CPUState *cpu, target_ulong base,
                                       target_ulong limit)
{}

static void kd_init_state_change(CPUState *cpu,
                                 DBGKD_ANY_WAIT_STATE_CHANGE *sc)
{
    CPUArchState *env = cpu->env_ptr;
    DBGKD_CONTROL_REPORT *cr = &sc->ControlReport;
    int err = 0;

    /* T0D0: HEADER */

    sc->Processor = 0;

    sc->NumberProcessors = 0;
    CPUState *cpu_tmp;
    CPU_FOREACH(cpu_tmp) {
        sc->NumberProcessors++;
    }
    sc->NumberProcessors = ldl_p(&sc->NumberProcessors);

    target_ulong KPRCB = READ_VMEM(cpu, kd->KPCR.addr +
                                   OFFSET_KPRCB, target_ulong);
    sc->Thread = READ_VMEM(cpu, KPRCB + OFFSET_KPRCB_CURRTHREAD,
                           target_ulong);
    sc->Thread = ldtul_p(&sc->Thread);
    sc->ProgramCounter = ldtul_p(&env->eip);

    /* T0D0: CONTROL REPORT */

    cr->Dr6 = ldtul_p(&env->dr[6]);
    cr->Dr7 = ldtul_p(&env->dr[7]);
    cr->ReportFlags = REPORT_INCLUDES_SEGS | REPORT_STANDARD_CS;
    cr->ReportFlags = lduw_p(&cr->ReportFlags);
    cr->SegCs = lduw_p(&env->segs[R_CS].selector);
    cr->SegDs = lduw_p(&env->segs[R_DS].selector);
    cr->SegEs = lduw_p(&env->segs[R_ES].selector);
    cr->SegFs = lduw_p(&env->segs[R_FS].selector);
    cr->EFlags = ldl_p(&env->eflags);

    err = cpu_memory_rw_debug(cpu, sc->ProgramCounter,
                              PTR(cr->InstructionStream[0]),
                              DBGKD_MAXSTREAM, 0);
    if (!err) {
        cr->InstructionCount = DBGKD_MAXSTREAM;
        cr->InstructionCount = lduw_p(&cr->InstructionCount);
        kd_breakpoint_remove_range(cpu, sc->ProgramCounter,
                                   sc->ProgramCounter + DBGKD_MAXSTREAM);
    }
}

SizedBuf kd_gen_exception_sc(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    SizedBuf buf;
    SBUF_MALLOC(buf, sizeof(DBGKD_ANY_WAIT_STATE_CHANGE) + sizeof(int));

    DBGKD_ANY_WAIT_STATE_CHANGE *sc = (DBGKD_ANY_WAIT_STATE_CHANGE *) buf.data;
    kd_init_state_change(cpu, sc);

    sc->NewState = DbgKdExceptionStateChange;
    sc->NewState = ldl_p(&sc->NewState);

    DBGKM_EXCEPTION_RECORD64 *exc = &sc->u.Exception.ExceptionRecord;
    exc->ExceptionCode = 0x80000003;
    exc->ExceptionCode = ldl_p(&exc->ExceptionCode);
    exc->ExceptionAddress = ldtul_p(&env->eip);

    return buf;
}

SizedBuf kd_gen_load_symbols_sc(CPUState *cpu)
{
    SizedBuf buf;
    SBUF_MALLOC(buf, sizeof(DBGKD_ANY_WAIT_STATE_CHANGE));

    DBGKD_ANY_WAIT_STATE_CHANGE *sc = (DBGKD_ANY_WAIT_STATE_CHANGE *) buf.data;
    kd_init_state_change(cpu, sc);

    sc->NewState = DbgKdLoadSymbolsStateChange;
    sc->NewState = ldl_p(&sc->NewState);

    sc->u.LoadSymbols.PathNameLength = 0;

    return buf;
}

bool windbg_on_load(void)
{
    CPUState *cpu = qemu_get_cpu(0);
    CPUArchState *env = cpu->env_ptr;

    if (!kd) {
        kd = g_new0(KDData, 1);
    }

    if (!kd->KPCR.is_init) {

 #ifdef TARGET_X86_64
        kd->KPCR.addr = env->segs[R_GS].base;
 #else
        kd->KPCR.addr = env->segs[R_FS].base;
 #endif

        static target_ulong prev_KPCR;
        if (!kd->KPCR.addr || prev_KPCR == kd->KPCR.addr) {
            return false;
        }
        prev_KPCR = kd->KPCR.addr;

        if (kd->KPCR.addr != READ_VMEM(cpu, kd->KPCR.addr + OFFSET_SELF_PCR,
                                       target_ulong)) {
            return false;
        }

        kd->KPCR.is_init = true;
    }

    if (!kd->version.is_init && kd->KPCR.is_init) {
        kd->version.addr = READ_VMEM(cpu, kd->KPCR.addr + OFFSET_VERS,
                                     target_ulong);
        if (!kd->version.addr) {
            return false;
        }
        kd->version.is_init = true;
    }

    WINDBG_DEBUG("windbg_on_load: KPCR " FMT_ADDR, kd->KPCR.addr);
    WINDBG_DEBUG("windbg_on_load: version " FMT_ADDR, kd->version.addr);

    return true;
}

void windbg_on_exit(void)
{
    g_free(kd);
    kd = NULL;
}
