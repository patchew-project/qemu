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
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#define IS_LOCAL_BP_ENABLED(dr7, index) (((dr7) >> ((index) * 2)) & 1)

#define IS_GLOBAL_BP_ENABLED(dr7, index) (((dr7) >> ((index) * 2)) & 2)

#define IS_BP_ENABLED(dr7, index) \
    (IS_LOCAL_BP_ENABLED(dr7, index) | IS_GLOBAL_BP_ENABLED(dr7, index))

#define BP_TYPE(dr7, index) \
    ((int) ((dr7) >> (DR7_TYPE_SHIFT + ((index) * 4))) & 3)

#define BP_LEN(dr7, index) ({                                    \
    int _len = (((dr7) >> (DR7_LEN_SHIFT + ((index) * 4))) & 3); \
    (_len == 2) ? 8 : _len + 1;                                  \
})

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

    InitedAddr bps[KD_BREAKPOINT_MAX];
} KDData;

static KDData *kd;

static int windbg_hw_breakpoint_insert(CPUState *cpu, int index)
{
    CPUArchState *env = cpu->env_ptr;

    if (!IS_BP_ENABLED(env->dr[7], index)) {
        return 0;
    }

    target_ulong addr = env->dr[index];
    int type = BP_TYPE(env->dr[7], index);
    int len = BP_LEN(env->dr[7], index);
    int err = 0;

    switch (type) {
    case DR7_TYPE_DATA_WR:
        err = cpu_watchpoint_insert(cpu, addr, len, BP_MEM_WRITE | BP_GDB,
                                    &env->cpu_watchpoint[index]);
        break;
    case DR7_TYPE_DATA_RW:
        err = cpu_watchpoint_insert(cpu, addr, len, BP_MEM_ACCESS | BP_GDB,
                                    &env->cpu_watchpoint[index]);
        break;
    case DR7_TYPE_BP_INST:
        err = cpu_breakpoint_insert(cpu, addr, BP_GDB,
                                    &env->cpu_breakpoint[index]);
        break;
    case DR7_TYPE_IO_RW:
        return HF_IOBPT_MASK;
    default:
        return 0;
    }

    if (!err) {
        WINDBG_DEBUG("hw_breakpoint_insert: index(%d), " FMT_ADDR,
                     index, addr);
    } else {
        env->cpu_breakpoint[index] = NULL;
        WINDBG_ERROR("hw_breakpoint_insert: index(%d), " FMT_ADDR ", " FMT_ERR,
                     index, addr, err);
    }
    return 0;
}

static int windbg_hw_breakpoint_remove(CPUState *cpu, int index)
{
    CPUArchState *env = cpu->env_ptr;
    int type = BP_TYPE(env->dr[7], index);

    switch (type) {
    case DR7_TYPE_BP_INST:
        if (env->cpu_breakpoint[index]) {
            cpu_breakpoint_remove_by_ref(cpu, env->cpu_breakpoint[index]);
        }
        break;
    case DR7_TYPE_DATA_WR:
    case DR7_TYPE_DATA_RW:
        if (env->cpu_watchpoint[index]) {
            cpu_watchpoint_remove_by_ref(cpu, env->cpu_watchpoint[index]);
        }
        break;
    default:
        return 0;
    }

    env->cpu_breakpoint[index] = NULL;
    WINDBG_DEBUG("hw_breakpoint_remove: index(%d), " FMT_ADDR,
                 index, env->dr[index]);
    return 0;
}

static void windbg_set_dr7(CPUState *cpu, target_ulong new_dr7)
{
    CPUArchState *env = cpu->env_ptr;
    target_ulong old_dr7 = env->dr[7];
    int iobpt = 0;
    int i;

    new_dr7 |= DR7_FIXED_1;
    if (new_dr7 == old_dr7) {
        return;
    }

    for (i = 0; i < DR7_MAX_BP; i++) {
        if (IS_BP_ENABLED(old_dr7, i) && !IS_BP_ENABLED(new_dr7, i)) {
            windbg_hw_breakpoint_remove(cpu, i);
        }
    }

    env->dr[7] = new_dr7;
    for (i = 0; i < DR7_MAX_BP; i++) {
        if (IS_BP_ENABLED(env->dr[7], i)) {
            iobpt |= windbg_hw_breakpoint_insert(cpu, i);
        }
    }

    env->hflags = (env->hflags & ~HF_IOBPT_MASK) | iobpt;
}

static void windbg_set_dr(CPUState *cpu, int index, target_ulong value)
{
    CPUArchState *env = cpu->env_ptr;

    switch (index) {
    case 0 ... 3:
        if (IS_BP_ENABLED(env->dr[7], index) && env->dr[index] != value) {
            windbg_hw_breakpoint_remove(cpu, index);
            env->dr[index] = value;
            windbg_hw_breakpoint_insert(cpu, index);
        } else {
            env->dr[index] = value;
        }
        return;
    case 6:
        env->dr[6] = value | DR6_FIXED_1;
        return;
    case 7:
        windbg_set_dr7(cpu, value);
        return;
    }
}

static void windbg_set_sr(CPUState *cpu, int sr, uint16_t selector)
{
    CPUArchState *env = cpu->env_ptr;

    if (selector != env->segs[sr].selector &&
        (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK))) {
        unsigned int limit, flags;
        target_ulong base;

        int dpl = (env->eflags & VM_MASK) ? 3 : 0;
        base = selector << 4;
        limit = 0xffff;
        flags = DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                DESC_A_MASK | (dpl << DESC_DPL_SHIFT);
        cpu_x86_load_seg_cache(env, sr, selector, base, limit, flags);
    }
}

static int windbg_read_context(CPUState *cpu, uint8_t *buf, int len,
                               int offset)
{
    const bool new_mem = (len != sizeof(CPU_CONTEXT) || offset != 0);
    CPUArchState *env = cpu->env_ptr;
    CPU_CONTEXT *cc;
    int err = 0;

    if (new_mem) {
        cc = g_new(CPU_CONTEXT, 1);
    } else {
        cc = (CPU_CONTEXT *) buf;
    }

    memset(cc, 0, len);

    cc->ContextFlags = CPU_CONTEXT_ALL;

    if (cc->ContextFlags & CPU_CONTEXT_SEGMENTS) {
        cc->SegCs = lduw_p(&env->segs[R_CS].selector);
        cc->SegDs = lduw_p(&env->segs[R_DS].selector);
        cc->SegEs = lduw_p(&env->segs[R_ES].selector);
        cc->SegFs = lduw_p(&env->segs[R_FS].selector);
        cc->SegGs = lduw_p(&env->segs[R_GS].selector);
        cc->SegSs = lduw_p(&env->segs[R_SS].selector);
    }

    if (cc->ContextFlags & CPU_CONTEXT_DEBUG_REGISTERS) {
        cc->Dr0 = ldtul_p(&env->dr[0]);
        cc->Dr1 = ldtul_p(&env->dr[1]);
        cc->Dr2 = ldtul_p(&env->dr[2]);
        cc->Dr3 = ldtul_p(&env->dr[3]);
        cc->Dr6 = ldtul_p(&env->dr[6]);
        cc->Dr7 = ldtul_p(&env->dr[7]);
    }

    if (cc->ContextFlags & CPU_CONTEXT_INTEGER) {
        cc->Edi    = ldl_p(&env->regs[R_EDI]);
        cc->Esi    = ldl_p(&env->regs[R_ESI]);
        cc->Ebx    = ldl_p(&env->regs[R_EBX]);
        cc->Edx    = ldl_p(&env->regs[R_EDX]);
        cc->Ecx    = ldl_p(&env->regs[R_ECX]);
        cc->Eax    = ldl_p(&env->regs[R_EAX]);
        cc->Ebp    = ldl_p(&env->regs[R_EBP]);
        cc->Esp    = ldl_p(&env->regs[R_ESP]);

        cc->Eip    = ldl_p(&env->eip);
        cc->EFlags = ldl_p(&env->eflags);
    }

    if (cc->ContextFlags & CPU_CONTEXT_FLOATING_POINT) {
        uint32_t swd = 0, twd = 0;
        swd = env->fpus & ~(7 << 11);
        swd |= (env->fpstt & 7) << 11;
        int i;
        for (i = 0; i < 8; ++i) {
            twd |= (!env->fptags[i]) << i;
        }

        cc->FloatSave.ControlWord    = ldl_p(&env->fpuc);
        cc->FloatSave.StatusWord     = ldl_p(&swd);
        cc->FloatSave.TagWord        = ldl_p(&twd);
        cc->FloatSave.ErrorOffset    = ldl_p(PTR(env->fpip));
        cc->FloatSave.ErrorSelector  = ldl_p(PTR(env->fpip) + 32);
        cc->FloatSave.DataOffset     = ldl_p(PTR(env->fpdp));
        cc->FloatSave.DataSelector   = ldl_p(PTR(env->fpdp) + 32);
        cc->FloatSave.Cr0NpxState    = ldl_p(&env->xcr0);

        for (i = 0; i < 8; ++i) {
            memcpy(PTR(cc->FloatSave.RegisterArea[i * 10]),
                   PTR(env->fpregs[i]), 10);
        }
    }

    if (cc->ContextFlags & CPU_CONTEXT_EXTENDED_REGISTERS) {
        uint8_t *ptr = cc->ExtendedRegisters + 160;
        int i;
        for (i = 0; i < 8; ++i, ptr += 16) {
            stq_p(ptr,     env->xmm_regs[i].ZMM_Q(0));
            stq_p(ptr + 8, env->xmm_regs[i].ZMM_Q(1));
        }

        stl_p(cc->ExtendedRegisters + 24, env->mxcsr);
    }

    cc->ContextFlags = ldl_p(&cc->ContextFlags);

    if (new_mem) {
        memcpy(buf, (uint8_t *) cc + offset, len);
        g_free(cc);
    }
    return err;
}

static int windbg_write_context(CPUState *cpu, uint8_t *buf, int len,
                                int offset)
{
  #ifdef TARGET_X86_64 /*Unimplemented yet */
    return 0;
  #else

    CPUArchState *env = cpu->env_ptr;
    int mem_size, i, tmp;
    uint8_t *mem_ptr = buf;

    while (len > 0 && offset < sizeof(CPU_CONTEXT)) {
        mem_size = 1;
        switch (offset) {

        case offsetof(CPU_CONTEXT, ContextFlags):
            mem_size = sizeof_field(CPU_CONTEXT, ContextFlags);
            break;

        case offsetof(CPU_CONTEXT, Dr0):
            mem_size = sizeof_field(CPU_CONTEXT, Dr0);
            windbg_set_dr(cpu, 0, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Dr1):
            mem_size = sizeof_field(CPU_CONTEXT, Dr1);
            windbg_set_dr(cpu, 1, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Dr2):
            mem_size = sizeof_field(CPU_CONTEXT, Dr2);
            windbg_set_dr(cpu, 2, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Dr3):
            mem_size = sizeof_field(CPU_CONTEXT, Dr3);
            windbg_set_dr(cpu, 3, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Dr6):
            mem_size = sizeof_field(CPU_CONTEXT, Dr6);
            windbg_set_dr(cpu, 6, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Dr7):
            mem_size = sizeof_field(CPU_CONTEXT, Dr7);
            windbg_set_dr(cpu, 7, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, FloatSave.ControlWord):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.ControlWord);
            cpu_set_fpuc(env, ldl_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, FloatSave.StatusWord):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.StatusWord);
            tmp = ldl_p(buf + offset);
            env->fpstt = (tmp >> 11) & 7;
            env->fpus = tmp & ~0x3800;
            break;

        case offsetof(CPU_CONTEXT, FloatSave.TagWord):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.TagWord);
            tmp = ldl_p(buf + offset);
            for (i = 0; i < 8; ++i) {
                env->fptags[i] = !((tmp >> i) & 1);
            }
            break;

        case offsetof(CPU_CONTEXT, FloatSave.ErrorOffset):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.ErrorOffset);
            UINT32_P(&env->fpip)[0] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, FloatSave.ErrorSelector):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.ErrorSelector);
            UINT32_P(&env->fpip)[1] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, FloatSave.DataOffset):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.DataOffset);
            UINT32_P(&env->fpdp)[0] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, FloatSave.DataSelector):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.DataSelector);
            UINT32_P(&env->fpdp)[1] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, FloatSave.RegisterArea):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.RegisterArea);
            for (i = 0; i < 8; ++i) {
                memcpy(PTR(env->fpregs[i]), mem_ptr + i * 10, 10);
            }
            break;

        case offsetof(CPU_CONTEXT, FloatSave.Cr0NpxState):
            mem_size = sizeof_field(CPU_CONTEXT, FloatSave.Cr0NpxState);
            env->xcr0 = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, SegGs):
            mem_size = sizeof_field(CPU_CONTEXT, SegGs);
            windbg_set_sr(cpu, R_GS, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, SegFs):
            mem_size = sizeof_field(CPU_CONTEXT, SegFs);
            windbg_set_sr(cpu, R_FS, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, SegEs):
            mem_size = sizeof_field(CPU_CONTEXT, SegEs);
            windbg_set_sr(cpu, R_ES, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, SegDs):
            mem_size = sizeof_field(CPU_CONTEXT, SegDs);
            windbg_set_sr(cpu, R_DS, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, Edi):
            mem_size = sizeof_field(CPU_CONTEXT, Edi);
            env->regs[R_EDI] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Esi):
            mem_size = sizeof_field(CPU_CONTEXT, Esi);
            env->regs[R_ESI] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Ebx):
            mem_size = sizeof_field(CPU_CONTEXT, Ebx);
            env->regs[R_EBX] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Edx):
            mem_size = sizeof_field(CPU_CONTEXT, Edx);
            env->regs[R_EDX] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Ecx):
            mem_size = sizeof_field(CPU_CONTEXT, Ecx);
            env->regs[R_ECX] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Eax):
            mem_size = sizeof_field(CPU_CONTEXT, Eax);
            env->regs[R_EAX] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Ebp):
            mem_size = sizeof_field(CPU_CONTEXT, Ebp);
            env->regs[R_EBP] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Eip):
            mem_size = sizeof_field(CPU_CONTEXT, Eip);
            env->eip = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, SegCs):
            mem_size = sizeof_field(CPU_CONTEXT, SegCs);
            windbg_set_sr(cpu, R_CS, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, EFlags):
            mem_size = sizeof_field(CPU_CONTEXT, EFlags);
            env->eflags = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, Esp):
            mem_size = sizeof_field(CPU_CONTEXT, Esp);
            env->regs[R_ESP] = ldl_p(buf + offset);
            break;

        case offsetof(CPU_CONTEXT, SegSs):
            mem_size = sizeof_field(CPU_CONTEXT, SegSs);
            windbg_set_sr(cpu, R_SS, lduw_p(buf + offset));
            break;

        case offsetof(CPU_CONTEXT, ExtendedRegisters):
            mem_size = sizeof_field(CPU_CONTEXT, ExtendedRegisters);

            uint8_t *ptr = mem_ptr + 160;
            for (i = 0; i < 8; ++i, ptr += 16) {
                env->xmm_regs[i].ZMM_Q(0) = ldl_p(ptr);
                env->xmm_regs[i].ZMM_Q(1) = ldl_p(ptr + 8);
            }

            cpu_set_mxcsr(env, ldl_p(mem_ptr + 24));
            break;

        default:
            WINDBG_ERROR("write_context: Unknown offset %d", offset);
            return -1;
        }

        mem_ptr += mem_size;
        offset += mem_size;
        len -= mem_size;
    }

    return 0;
  #endif
}

static int windbg_read_ks_regs(CPUState *cpu, uint8_t *buf, int len,
                               int offset)
{
    CPUArchState *env = cpu->env_ptr;
    const bool new_mem = (len != sizeof(CPU_KSPECIAL_REGISTERS)
                       || offset != 0);
    CPU_KSPECIAL_REGISTERS *ckr;
    if (new_mem) {
        ckr = g_new(CPU_KSPECIAL_REGISTERS, 1);
    } else {
        ckr = (CPU_KSPECIAL_REGISTERS *) buf;
    }

    memset(ckr, 0, len);

    ckr->Cr0 = ldl_p(&env->cr[0]);
    ckr->Cr2 = ldl_p(&env->cr[2]);
    ckr->Cr3 = ldl_p(&env->cr[3]);
    ckr->Cr4 = ldl_p(&env->cr[4]);

    ckr->KernelDr0 = ldtul_p(&env->dr[0]);
    ckr->KernelDr1 = ldtul_p(&env->dr[1]);
    ckr->KernelDr2 = ldtul_p(&env->dr[2]);
    ckr->KernelDr3 = ldtul_p(&env->dr[3]);
    ckr->KernelDr6 = ldtul_p(&env->dr[6]);
    ckr->KernelDr7 = ldtul_p(&env->dr[7]);

    ckr->Gdtr.Pad = lduw_p(&env->gdt.selector);
    ckr->Idtr.Pad = lduw_p(&env->idt.selector);

    ckr->Gdtr.Limit = lduw_p(&env->gdt.limit);
    ckr->Gdtr.Base  = ldtul_p(&env->gdt.base);
    ckr->Idtr.Limit = lduw_p(&env->idt.limit);
    ckr->Idtr.Base  = ldtul_p(&env->idt.base);
    ckr->Tr         = lduw_p(&env->tr.selector);
    ckr->Ldtr       = lduw_p(&env->ldt.selector);

    if (new_mem) {
        memcpy(buf, (uint8_t *) ckr + offset, len);
        g_free(ckr);
    }
    return 0;
}

static int windbg_write_ks_regs(CPUState *cpu, uint8_t *buf, int len,
                                int offset)
{
  #ifdef TARGET_X86_64 /* Unimplemented yet */
    return 0;
  #else

    CPUArchState *env = cpu->env_ptr;
    int mem_size;
    uint8_t *mem_ptr = buf;
    while (len > 0 && offset < sizeof(CPU_KSPECIAL_REGISTERS)) {
        mem_size = 1;
        switch (offset) {

        case offsetof(CPU_KSPECIAL_REGISTERS, Cr0):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Cr0);
            cpu_x86_update_cr0(env, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Cr2):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Cr2);
            env->cr[2] = ldtul_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Cr3):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Cr3);
            cpu_x86_update_cr3(env, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Cr4):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Cr4);
            cpu_x86_update_cr4(env, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr0):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr0);
            windbg_set_dr(cpu, 0, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr1):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr1);
            windbg_set_dr(cpu, 1, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr2):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr2);
            windbg_set_dr(cpu, 2, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr3):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr3);
            windbg_set_dr(cpu, 3, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr6):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr6);
            windbg_set_dr(cpu, 6, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, KernelDr7):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, KernelDr7);
            windbg_set_dr(cpu, 7, ldtul_p(buf + offset));
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Gdtr.Pad):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Gdtr.Pad);
            env->gdt.selector = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Gdtr.Limit):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Gdtr.Limit);
            env->gdt.limit = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Gdtr.Base):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Gdtr.Base);
            env->gdt.base = ldtul_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Idtr.Pad):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Idtr.Pad);
            env->idt.selector = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Idtr.Limit):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Idtr.Limit);
            env->idt.limit = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Idtr.Base):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Idtr.Base);
            env->idt.base = ldtul_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Tr):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Tr);
            env->tr.selector = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Ldtr):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Ldtr);
            env->ldt.selector = lduw_p(buf + offset);
            break;

        case offsetof(CPU_KSPECIAL_REGISTERS, Reserved):
            mem_size = sizeof_field(CPU_KSPECIAL_REGISTERS, Reserved);
            break;

        default:
            WINDBG_ERROR("write_context: Unknown offset %d", offset);
            return -1;
        }

        mem_ptr += mem_size;
        offset += mem_size;
        len -= mem_size;
    }

    return 0;

  #endif
}

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

void kd_api_get_context(CPUState *cpu, PacketData *pd)
{
    int err;

    pd->extra_size = sizeof(CPU_CONTEXT);
    err = windbg_read_context(cpu, pd->extra, pd->extra_size, 0);

    if (err) {
        pd->extra_size = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_set_context(CPUState *cpu, PacketData *pd)
{
    int err;

    err = windbg_write_context(cpu, pd->extra, pd->extra_size, 0);
    pd->extra_size = 0;

    if (err) {
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_write_breakpoint(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_BREAKPOINT64 *m64c = &pd->m64.u.WriteBreakPoint;
    target_ulong addr = ldtul_p(&m64c->BreakPointAddress);
    int i = 0, err = 0;

    for (; i < KD_BREAKPOINT_MAX; ++i) {
        if (!kd->bps[i].is_init) {
            err = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
            if (!err) {
                kd->bps[i].addr = addr;
                kd->bps[i].is_init = true;
                WINDBG_DEBUG("write_breakpoint: " FMT_ADDR, addr);
                break;
            } else {
                WINDBG_ERROR("write_breakpoint: " FMT_ADDR ", " FMT_ERR,
                             addr, err);
                pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
                return;
            }
        } else if (addr == kd->bps[i].addr) {
            break;
        }
    }

    if (!err) {
        m64c->BreakPointHandle = i + 1;
        pd->m64.ReturnStatus = STATUS_SUCCESS;
    } else {
        WINDBG_ERROR("write_breakpoint: All breakpoints occupied");
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_restore_breakpoint(CPUState *cpu, PacketData *pd)
{
    DBGKD_RESTORE_BREAKPOINT *m64c = &pd->m64.u.RestoreBreakPoint;
    uint8_t index = ldtul_p(&m64c->BreakPointHandle) - 1;
    int err = -1;

    if (kd->bps[index].is_init) {
        err = cpu_breakpoint_remove(cpu, kd->bps[index].addr, BP_GDB);
        if (!err) {
            WINDBG_DEBUG("restore_breakpoint: " FMT_ADDR ", index(%d)",
                         kd->bps[index].addr, index);
        } else {
            WINDBG_ERROR("restore_breakpoint: " FMT_ADDR ", index(%d), "
                         FMT_ERR, kd->bps[index].addr, index, err);
        }
        kd->bps[index].is_init = false;
        pd->m64.ReturnStatus = STATUS_SUCCESS;
    } else {
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_continue(CPUState *cpu, PacketData *pd)
{
    uint32_t status = ldl_p(&pd->m64.u.Continue2.ContinueStatus);
    uint32_t trace = ldl_p(&pd->m64.u.Continue2.ControlSet.TraceFlag);
    int ssFlag = trace ? SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER : 0;

    if (NT_SUCCESS(status)) {
        cpu_single_step(cpu, ssFlag);
        if (!runstate_needs_reset()) {
            vm_start();
        }
    }
}

void kd_api_read_control_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_MEMORY64 *mem = &pd->m64.u.ReadMemory;
    uint32_t len;
    target_ulong addr;
    int err = -1;

    len = MIN(ldl_p(&mem->TransferCount), PACKET_MAX_SIZE - M64_SIZE);
    addr = ldtul_p(&mem->TargetBaseAddress);

    if (addr < sizeof(CPU_KPROCESSOR_STATE)) {
        len = MIN(len, sizeof(CPU_KPROCESSOR_STATE) - addr);

        uint32_t from_context = MAX(0, (int) (sizeof(CPU_CONTEXT) - addr));
        uint32_t from_ks_regs = len - from_context;

        if (from_context > 0) {
            err = windbg_read_context(cpu, pd->extra, from_context, addr);
        }
        if (from_ks_regs > 0) {
            err = windbg_read_ks_regs(cpu, pd->extra + from_context,
                                      from_ks_regs, addr -
                                      sizeof(CPU_CONTEXT) + from_context);
        }
    }

    if (!err) {
        pd->extra_size = len;
        mem->ActualBytesRead = ldl_p(&len);
    } else {
        pd->extra_size = mem->ActualBytesRead = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_write_control_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_MEMORY64 *mem = &pd->m64.u.WriteMemory;
    uint32_t len;
    target_ulong addr;
    int err = -1;

    len = MIN(ldl_p(&mem->TransferCount), pd->extra_size);
    addr = ldtul_p(&mem->TargetBaseAddress);

    if (addr < sizeof(CPU_KPROCESSOR_STATE)) {
        len = MIN(len, sizeof(CPU_KPROCESSOR_STATE) - addr);

        uint32_t to_context = MAX(0, (int) (sizeof(CPU_CONTEXT) - addr));
        uint32_t to_ks_regs = len - to_context;

        if (to_context > 0) {
            err = windbg_write_context(cpu, pd->extra, to_context, addr);
        }
        if (to_ks_regs > 0) {
            err = windbg_write_ks_regs(cpu, pd->extra + to_context, to_ks_regs,
                                       addr - sizeof(CPU_CONTEXT) + to_context);
        }
    }

    pd->extra_size = 0;
    if (!err) {
        mem->ActualBytesWritten = ldl_p(&len);
    } else {
        mem->ActualBytesWritten = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_read_io_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_IO64 *io = &pd->m64.u.ReadWriteIo;
    CPUArchState *env = cpu->env_ptr;
    target_ulong addr = ldtul_p(&io->IoAddress);

    switch (io->DataSize) {
    case 1:
        io->DataValue = address_space_ldub(&address_space_io, addr,
                                           cpu_get_mem_attrs(env), NULL);
        break;
    case 2:
        io->DataValue = address_space_lduw(&address_space_io, addr,
                                           cpu_get_mem_attrs(env), NULL);
        break;
    case 4:
        io->DataValue = address_space_ldl(&address_space_io, addr,
                                          cpu_get_mem_attrs(env), NULL);
        break;
    default:
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        return;
    }

    pd->m64.ReturnStatus = STATUS_SUCCESS;
}

void kd_api_write_io_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_IO64 *io = &pd->m64.u.ReadWriteIo;
    CPUArchState *env = cpu->env_ptr;

    target_ulong addr = ldtul_p(&io->IoAddress);
    uint32_t value = ldl_p(&io->DataValue);

    switch (io->DataSize) {
    case 1:
        address_space_stb(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    case 2:
        address_space_stw(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    case 4:
        address_space_stl(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    default:
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        return;
    }

    pd->m64.ReturnStatus = STATUS_SUCCESS;
}

void kd_api_read_physical_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_MEMORY64 *mem = &pd->m64.u.ReadMemory;
    uint32_t len;
    target_ulong addr;

    len = MIN(ldl_p(&mem->TransferCount), PACKET_MAX_SIZE - M64_SIZE);
    addr = ldtul_p(&mem->TargetBaseAddress);

    cpu_physical_memory_rw(addr, pd->extra, len, 0);
    pd->extra_size = len;
    mem->ActualBytesRead = ldl_p(&len);
}

void kd_api_write_physical_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_MEMORY64 *mem = &pd->m64.u.WriteMemory;
    uint32_t len;
    target_ulong addr;

    len = MIN(ldl_p(&mem->TransferCount), pd->extra_size);
    addr = ldtul_p(&mem->TargetBaseAddress);

    cpu_physical_memory_rw(addr, pd->extra, len, 1);
    pd->extra_size = 0;
    mem->ActualBytesWritten = ldl_p(&len);
}

void kd_api_get_version(CPUState *cpu, PacketData *pd)
{
    DBGKD_GET_VERSION64 *version;
    int err = cpu_memory_rw_debug(cpu, kd->version.addr,
                                  PTR(pd->m64) + 0x10,
                                  M64_SIZE - 0x10, 0);
    if (!err) {
        version = (DBGKD_GET_VERSION64 *) (PTR(pd->m64) + 0x10);

        version->MajorVersion = lduw_p(&version->MajorVersion);
        version->MinorVersion = lduw_p(&version->MinorVersion);
        version->Flags = lduw_p(&version->Flags);
        version->MachineType = lduw_p(&version->MachineType);
        version->Unused[0] = lduw_p(version->Unused);
        version->KernBase = ldq_p(&version->KernBase);
        version->PsLoadedModuleList = ldq_p(&version->PsLoadedModuleList);
        version->DebuggerDataList = ldq_p(&version->DebuggerDataList);
    } else {
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        WINDBG_ERROR("get_version: " FMT_ERR, err);
    }
}

void kd_api_read_msr(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_MSR *m64c = &pd->m64.u.ReadWriteMsr;
    CPUArchState *env = cpu->env_ptr;

    uint64_t val;

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 0, 0);

    switch ((uint32_t)env->regs[R_ECX]) {
    case MSR_IA32_SYSENTER_CS:
        val = env->sysenter_cs;
        break;
    case MSR_IA32_SYSENTER_ESP:
        val = env->sysenter_esp;
        break;
    case MSR_IA32_SYSENTER_EIP:
        val = env->sysenter_eip;
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(x86_env_get_cpu(env)->apic_state);
        break;
    case MSR_EFER:
        val = env->efer;
        break;
    case MSR_STAR:
        val = env->star;
        break;
    case MSR_PAT:
        val = env->pat;
        break;
    case MSR_VM_HSAVE_PA:
        val = env->vm_hsave;
        break;
    case MSR_IA32_PERF_STATUS:
        /* tsc_increment_by_tick */
        val = 1000ULL;
        /* CPU multiplier */
        val |= (((uint64_t)4ULL) << 40);
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        val = env->lstar;
        break;
    case MSR_CSTAR:
        val = env->cstar;
        break;
    case MSR_FMASK:
        val = env->fmask;
        break;
    case MSR_FSBASE:
        val = env->segs[R_FS].base;
        break;
    case MSR_GSBASE:
        val = env->segs[R_GS].base;
        break;
    case MSR_KERNELGSBASE:
        val = env->kernelgsbase;
        break;
    case MSR_TSC_AUX:
        val = env->tsc_aux;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    case MSR_MTRRcap:
        if (env->features[FEAT_1_EDX] & CPUID_MTRR) {
            val = MSR_MTRRcap_VCNT | MSR_MTRRcap_FIXRANGE_SUPPORT |
                MSR_MTRRcap_WC_SUPPORTED;
        } else {
            /* XXX: exception? */
            val = 0;
        }
        break;
    case MSR_MCG_CAP:
        val = env->mcg_cap;
        break;
    case MSR_MCG_CTL:
        if (env->mcg_cap & MCG_CTL_P) {
            val = env->mcg_ctl;
        } else {
            val = 0;
        }
        break;
    case MSR_MCG_STATUS:
        val = env->mcg_status;
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    case MSR_IA32_BNDCFGS:
        val = env->msr_bndcfgs;
        break;
    default:
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            val = env->mce_banks[offset];
            break;
        }
        /* XXX: exception? */
        val = 0;
        break;
    }

    val = ldq_p(&val);
    m64c->DataValueLow  = UINT32_P(val)[0];
    m64c->DataValueHigh = UINT32_P(val)[1];
    pd->m64.ReturnStatus = STATUS_SUCCESS;
}

void kd_api_write_msr(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_MSR *m64c = &pd->m64.u.ReadWriteMsr;
    CPUArchState *env = cpu->env_ptr;

    uint64_t val;

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 1, 0);

    val = m64c->DataValueLow | ((uint64_t) m64c->DataValueHigh) << 32;
    val = ldq_p(&val);

    switch ((uint32_t)env->regs[R_ECX]) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = val & 0xffff;
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = val;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = val;
        break;
    case MSR_IA32_APICBASE:
        cpu_set_apic_base(x86_env_get_cpu(env)->apic_state, val);
        break;
    case MSR_EFER:
        {
            uint64_t update_mask;

            update_mask = 0;
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_SYSCALL) {
                update_mask |= MSR_EFER_SCE;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
                update_mask |= MSR_EFER_LME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_NX) {
                update_mask |= MSR_EFER_NXE;
            }
            if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
                update_mask |= MSR_EFER_SVME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            cpu_load_efer(env, (env->efer & ~update_mask) |
                          (val & update_mask));
        }
        break;
    case MSR_STAR:
        env->star = val;
        break;
    case MSR_PAT:
        env->pat = val;
        break;
    case MSR_VM_HSAVE_PA:
        env->vm_hsave = val;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        env->lstar = val;
        break;
    case MSR_CSTAR:
        env->cstar = val;
        break;
    case MSR_FMASK:
        env->fmask = val;
        break;
    case MSR_FSBASE:
        env->segs[R_FS].base = val;
        break;
    case MSR_GSBASE:
        env->segs[R_GS].base = val;
        break;
    case MSR_KERNELGSBASE:
        env->kernelgsbase = val;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysBase(0)) / 2].base = val;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysMask(0)) / 2].mask = val;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix64K_00000] = val;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix16K_80000 + 1] = val;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix4K_C0000 + 3] = val;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = val;
        break;
    case MSR_MCG_STATUS:
        env->mcg_status = val;
        break;
    case MSR_MCG_CTL:
        if ((env->mcg_cap & MCG_CTL_P)
            && (val == 0 || val == ~(uint64_t)0)) {
            env->mcg_ctl = val;
        }
        break;
    case MSR_TSC_AUX:
        env->tsc_aux = val;
        break;
    case MSR_IA32_MISC_ENABLE:
        env->msr_ia32_misc_enable = val;
        break;
    case MSR_IA32_BNDCFGS:
        /* FIXME: #GP if reserved bits are set.  */
        /* FIXME: Extend highest implemented bit of linear address.  */
        env->msr_bndcfgs = val;
        cpu_sync_bndcs_hflags(env);
        break;
    default:
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            if ((offset & 0x3) != 0
                || (val == 0 || val == ~(uint64_t)0)) {
                env->mce_banks[offset] = val;
            }
            break;
        }
        /* XXX: exception? */
        break;
    }

    pd->m64.ReturnStatus = STATUS_SUCCESS;
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
{
    int i = 0, err = 0;
    for (; i < KD_BREAKPOINT_MAX; ++i) {
        if (kd->bps[i].is_init && kd->bps[i].addr >= base &&
            kd->bps[i].addr < limit) {
            err = cpu_breakpoint_remove(cpu, kd->bps[i].addr, BP_GDB);
            if (!err) {
                WINDBG_DEBUG("breakpoint_remove_range: " FMT_ADDR
                             ", index(%d)", kd->bps[i].addr, i);
            } else {
                WINDBG_ERROR("breakpoint_remove_range: " FMT_ADDR
                             ", index(%d), " FMT_ERR,
                             kd->bps[i].addr, i, err);
            }
            kd->bps[i].is_init = false;
        }
    }
}

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
