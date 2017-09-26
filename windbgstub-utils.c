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

/*
 * Next code copied from winnt.h
 */
#ifdef TARGET_X86_64

#define CPU_CONTEXT_AMD64 0x100000

#define CPU_CONTEXT_CONTROL         (CPU_CONTEXT_AMD64 | 0x1)
#define CPU_CONTEXT_INTEGER         (CPU_CONTEXT_AMD64 | 0x2)
#define CPU_CONTEXT_SEGMENTS        (CPU_CONTEXT_AMD64 | 0x4)
#define CPU_CONTEXT_FLOATING_POINT  (CPU_CONTEXT_AMD64 | 0x8)
#define CPU_CONTEXT_DEBUG_REGISTERS (CPU_CONTEXT_AMD64 | 0x10)

#define CPU_CONTEXT_FULL \
    (CPU_CONTEXT_CONTROL | CPU_CONTEXT_INTEGER | CPU_CONTEXT_FLOATING_POINT)
#define CPU_CONTEXT_ALL \
    (CPU_CONTEXT_FULL | CPU_CONTEXT_SEGMENTS | CPU_CONTEXT_DEBUG_REGISTERS)

typedef struct _CPU_DESCRIPTOR {
    uint16_t Pad[3];
    uint16_t Limit;
    uint64_t Base;
} CPU_DESCRIPTOR, *PCPU_DESCRIPTOR;

typedef struct _CPU_KSPECIAL_REGISTERS {
    uint64_t Cr0;
    uint64_t Cr2;
    uint64_t Cr3;
    uint64_t Cr4;
    uint64_t KernelDr0;
    uint64_t KernelDr1;
    uint64_t KernelDr2;
    uint64_t KernelDr3;
    uint64_t KernelDr6;
    uint64_t KernelDr7;
    CPU_DESCRIPTOR Gdtr;
    CPU_DESCRIPTOR Idtr;
    uint16_t Tr;
    uint16_t Ldtr;
    uint32_t MxCsr;
    uint64_t DebugControl;
    uint64_t LastBranchToRip;
    uint64_t LastBranchFromRip;
    uint64_t LastExceptionToRip;
    uint64_t LastExceptionFromRip;
    uint64_t Cr8;
    uint64_t MsrGsBase;
    uint64_t MsrGsSwap;
    uint64_t MsrStar;
    uint64_t MsrLStar;
    uint64_t MsrCStar;
    uint64_t MsrSyscallMask;
    uint64_t Xcr0;
} CPU_KSPECIAL_REGISTERS, *PCPU_KSPECIAL_REGISTERS;

#pragma pack(push, 2)
typedef struct _CPU_M128A {
    uint64_t Low;
    int64_t High;
} CPU_M128A, *PCPU_M128A;
#pragma pack(pop)

typedef struct _CPU_XMM_SAVE_AREA32 {
    uint16_t ControlWord;
    uint16_t StatusWord;
    uint8_t TagWord;
    uint8_t Reserved1;
    uint16_t ErrorOpcode;
    uint32_t ErrorOffset;
    uint16_t ErrorSelector;
    uint16_t Reserved2;
    uint32_t DataOffset;
    uint16_t DataSelector;
    uint16_t Reserved3;
    uint32_t MxCsr;
    uint32_t MxCsr_Mask;
    CPU_M128A FloatRegisters[8];
    CPU_M128A XmmRegisters[16];
    uint8_t Reserved4[96];
} CPU_XMM_SAVE_AREA32, *PCPU_XMM_SAVE_AREA32;

#pragma pack(push, 2)
typedef struct _CPU_CONTEXT { /* sizeof = 1232 */
    uint64_t P1Home;
    uint64_t P2Home;
    uint64_t P3Home;
    uint64_t P4Home;
    uint64_t P5Home;
    uint64_t P6Home;
    uint32_t ContextFlags;
    uint32_t MxCsr;
    uint16_t SegCs;
    uint16_t SegDs;
    uint16_t SegEs;
    uint16_t SegFs;
    uint16_t SegGs;
    uint16_t SegSs;
    uint32_t EFlags;
    uint64_t Dr0;
    uint64_t Dr1;
    uint64_t Dr2;
    uint64_t Dr3;
    uint64_t Dr6;
    uint64_t Dr7;
    uint64_t Rax;
    uint64_t Rcx;
    uint64_t Rdx;
    uint64_t Rbx;
    uint64_t Rsp;
    uint64_t Rbp;
    uint64_t Rsi;
    uint64_t Rdi;
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
    uint64_t Rip;
    union {
        CPU_XMM_SAVE_AREA32 FltSave;
        CPU_XMM_SAVE_AREA32 FloatSave;
        struct {
            CPU_M128A Header[2];
            CPU_M128A Legacy[8];
            CPU_M128A Xmm0;
            CPU_M128A Xmm1;
            CPU_M128A Xmm2;
            CPU_M128A Xmm3;
            CPU_M128A Xmm4;
            CPU_M128A Xmm5;
            CPU_M128A Xmm6;
            CPU_M128A Xmm7;
            CPU_M128A Xmm8;
            CPU_M128A Xmm9;
            CPU_M128A Xmm10;
            CPU_M128A Xmm11;
            CPU_M128A Xmm12;
            CPU_M128A Xmm13;
            CPU_M128A Xmm14;
            CPU_M128A Xmm15;
        };
    };
    CPU_M128A VectorRegister[26];
    uint64_t VectorControl;
    uint64_t DebugControl;
    uint64_t LastBranchToRip;
    uint64_t LastBranchFromRip;
    uint64_t LastExceptionToRip;
    uint64_t LastExceptionFromRip;
} CPU_CONTEXT, *PCPU_CONTEXT;
#pragma pack(pop)

#else

#define SIZE_OF_X86_REG 80
#define MAX_SUP_EXT 512

#define CPU_CONTEXT_i386 0x10000

#define CPU_CONTEXT_CONTROL            (CPU_CONTEXT_i386 | 0x1)
#define CPU_CONTEXT_INTEGER            (CPU_CONTEXT_i386 | 0x2)
#define CPU_CONTEXT_SEGMENTS           (CPU_CONTEXT_i386 | 0x4)
#define CPU_CONTEXT_FLOATING_POINT     (CPU_CONTEXT_i386 | 0x8)
#define CPU_CONTEXT_DEBUG_REGISTERS    (CPU_CONTEXT_i386 | 0x10)
#define CPU_CONTEXT_EXTENDED_REGISTERS (CPU_CONTEXT_i386 | 0x20)

#define CPU_CONTEXT_FULL \
    (CPU_CONTEXT_CONTROL | CPU_CONTEXT_INTEGER | CPU_CONTEXT_SEGMENTS)
#define CPU_CONTEXT_ALL \
    (CPU_CONTEXT_FULL | CPU_CONTEXT_FLOATING_POINT | \
     CPU_CONTEXT_DEBUG_REGISTERS | CPU_CONTEXT_EXTENDED_REGISTERS)

typedef struct _CPU_DESCRIPTOR {
    uint16_t Pad;
    uint16_t Limit;
    uint32_t Base;
} CPU_DESCRIPTOR, *PCPU_DESCRIPTOR;

typedef struct _CPU_KSPECIAL_REGISTERS {
    uint32_t Cr0;
    uint32_t Cr2;
    uint32_t Cr3;
    uint32_t Cr4;
    uint32_t KernelDr0;
    uint32_t KernelDr1;
    uint32_t KernelDr2;
    uint32_t KernelDr3;
    uint32_t KernelDr6;
    uint32_t KernelDr7;
    CPU_DESCRIPTOR Gdtr;
    CPU_DESCRIPTOR Idtr;
    uint16_t Tr;
    uint16_t Ldtr;
    uint32_t Reserved[6];
} CPU_KSPECIAL_REGISTERS, *PCPU_KSPECIAL_REGISTERS;

typedef struct _CPU_FLOATING_SAVE_AREA {
    uint32_t ControlWord;
    uint32_t StatusWord;
    uint32_t TagWord;
    uint32_t ErrorOffset;
    uint32_t ErrorSelector;
    uint32_t DataOffset;
    uint32_t DataSelector;
    uint8_t RegisterArea[SIZE_OF_X86_REG];
    uint32_t Cr0NpxState;
} CPU_FLOATING_SAVE_AREA, *PCPU_FLOATING_SAVE_AREA;

typedef struct _CPU_CONTEXT { /* sizeof = 716 */
    uint32_t ContextFlags;
    uint32_t Dr0;
    uint32_t Dr1;
    uint32_t Dr2;
    uint32_t Dr3;
    uint32_t Dr6;
    uint32_t Dr7;
    CPU_FLOATING_SAVE_AREA FloatSave;
    uint32_t SegGs;
    uint32_t SegFs;
    uint32_t SegEs;
    uint32_t SegDs;

    uint32_t Edi;
    uint32_t Esi;
    uint32_t Ebx;
    uint32_t Edx;
    uint32_t Ecx;
    uint32_t Eax;
    uint32_t Ebp;
    uint32_t Eip;
    uint32_t SegCs;
    uint32_t EFlags;
    uint32_t Esp;
    uint32_t SegSs;
    uint8_t ExtendedRegisters[MAX_SUP_EXT];
} CPU_CONTEXT, *PCPU_CONTEXT;

typedef struct _CPU_KPROCESSOR_STATE {
    CPU_CONTEXT ContextFrame;
    CPU_KSPECIAL_REGISTERS SpecialRegisters;
} CPU_KPROCESSOR_STATE, *PCPU_KPROCESSOR_STATE;

#endif

typedef struct KDData {
    InitedAddr KPCR;
    InitedAddr version;
} KDData;

static KDData *kd;

void kd_api_read_virtual_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_MEMORY64 *mem = &pd->m64.u.ReadMemory;
    uint32_t len;
    target_ulong addr;
    int err;

    len = MIN(ldl_p(&mem->TransferCount), PACKET_MAX_SIZE - M64_SIZE);
    addr = ldtul_p(&mem->TargetBaseAddress);
    err = cpu_memory_rw_debug(cpu, addr, pd->extra, len, 0);

    if (!err) {
        pd->extra_size = len;
        mem->ActualBytesRead = ldl_p(&len);
    } else {
        pd->extra_size = 0;
        mem->ActualBytesRead = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;

        WINDBG_DEBUG("read_virtual_memory: No physical page mapped: " FMT_ADDR,
                     (target_ulong) mem->TargetBaseAddress);
    }
}

void kd_api_write_virtual_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_MEMORY64 *mem = &pd->m64.u.WriteMemory;
    uint32_t len;
    target_ulong addr;
    int err;

    len = MIN(ldl_p(&mem->TransferCount), pd->extra_size);
    addr = ldtul_p(&mem->TargetBaseAddress);
    err = cpu_memory_rw_debug(cpu, addr, pd->extra, len, 1);

    pd->extra_size = 0;
    if (!err) {
        mem->ActualBytesWritten = ldl_p(&len);
    } else {
        mem->ActualBytesWritten = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;

        WINDBG_DEBUG("read_write_memory: No physical page mapped: " FMT_ADDR,
                     (target_ulong) mem->TargetBaseAddress);
    }
}

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
