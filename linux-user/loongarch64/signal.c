/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation of Linux signals
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "signal-common.h"
#include "linux-user/trace.h"

#include "target/loongarch/internals.h"

/* FP context was used */
#define SC_USED_FP              (1 << 0)

struct target_sigcontext {
    uint64_t sc_pc;
    uint64_t sc_regs[32];
    uint32_t sc_flags;
    uint64_t sc_extcontext[0]   QEMU_ALIGNED(16);
};


#define FPU_CTX_MAGIC           0x46505501
#define FPU_CTX_ALIGN           8
struct target_fpu_context {
    uint64_t regs[32];
    uint64_t fcc;
    uint32_t fcsr;
};

#define CONTEXT_INFO_ALIGN      16
struct sctx_info {
    uint32_t magic;
    uint32_t size;
    uint64_t padding;           /* padding to 16 bytes */
};

struct target_ucontext {
    target_ulong tuc_flags;
    struct target_ucontext *tuc_link;
    target_stack_t tuc_stack;
    target_sigset_t tuc_sigmask;
    uint8_t __unused[1024 / 8 - sizeof(target_sigset_t)];
    struct target_sigcontext tuc_mcontext;
};

struct target_rt_sigframe {
    struct target_siginfo        rs_info;
    struct target_ucontext       rs_uc;
    struct sctx_info             rs_fpu_info QEMU_ALIGNED(16);
    struct target_fpu_context    rs_fpu;
    struct sctx_info             rs_end_info QEMU_ALIGNED(16);
};

struct ctx_layout {
    abi_ulong addr;
    unsigned int size;
};

struct extctx_layout {
    unsigned int size;
    unsigned int flags;
    struct ctx_layout fpu;
    struct ctx_layout end;
};

static uint64_t read_all_fcc(CPULoongArchState *env)
{
    uint64_t ret = 0;

    for (int i = 0; i < 8; ++i) {
        ret |= (uint64_t)env->cf[i] << (i * 8);
    }

    return ret;
}

static void write_all_fcc(CPULoongArchState *env, uint64_t val)
{
    for (int i = 0; i < 8; ++i) {
        env->cf[i] = (val >> (i * 8)) & 1;
    }
}

static void copy_fpu_to_sigframe(CPULoongArchState *env,
                                 struct target_rt_sigframe *frame,
                                 struct extctx_layout *extctx)
{
    int i;
    struct target_fpu_context *fpu_ctx = &frame->rs_fpu;

    for (i = 0; i < 32; ++i) {
        __put_user(env->fpr[i], &fpu_ctx->regs[i]);
    }

    fpu_ctx->fcc = read_all_fcc(env);
    __put_user(env->fcsr0, &fpu_ctx->fcsr);

    /* Set fpu_context info */
    __put_user(FPU_CTX_MAGIC, &frame->rs_fpu_info.magic);
    __put_user(extctx->fpu.size, &frame->rs_fpu_info.size);
}

static void setup_extcontext(struct extctx_layout *extctx, abi_ulong sp)
{
    memset(extctx, 0, sizeof(struct extctx_layout));

    extctx->end.addr = sp + offsetof(struct target_rt_sigframe, rs_end_info);
    extctx->end.size = sizeof(struct sctx_info);
    extctx->fpu.addr = sp + offsetof(struct target_rt_sigframe, rs_fpu_info);
    extctx->fpu.size = extctx->end.addr - extctx->fpu.addr;
    extctx->size = extctx->fpu.size + extctx->end.size;
    extctx->flags = SC_USED_FP;
}

static void setup_sigcontext(CPULoongArchState *env,
                             struct target_sigcontext *sc,
                             struct extctx_layout *extctx)
{
    int i;

    __put_user(extctx->flags, &sc->sc_flags);
    __put_user(env->pc, &sc->sc_pc);

    for (i = 1; i < 32; ++i) {
        __put_user(env->gpr[i], &sc->sc_regs[i]);
    }
}

static void copy_fpu_from_sigframe(CPULoongArchState *env,
                                   struct target_rt_sigframe *frame)
{
    int i;
    struct target_fpu_context *fpu_ctx = &frame->rs_fpu;

    for (i = 1; i < 32; ++i) {
        __get_user(env->fpr[i], &fpu_ctx->regs[i]);
    }
    write_all_fcc(env, fpu_ctx->fcc);
    __get_user(env->fcsr0, &fpu_ctx->fcsr);
}

static void setup_sigframe(CPULoongArchState *env,
                           struct target_rt_sigframe *frame,
                           struct extctx_layout *extctx)
{
    setup_sigcontext(env, &frame->rs_uc.tuc_mcontext, extctx);

    /* Set fpu_context */
    if (extctx->flags & SC_USED_FP) {
        copy_fpu_to_sigframe(env, frame, extctx);
    }

    /* Set end context */
    __put_user(0, &frame->rs_end_info.magic);
    __put_user(extctx->end.size, &frame->rs_end_info.size);
}

static int parse_extcontext(struct target_sigcontext *sc,
                            struct extctx_layout *extctx)
{
    unsigned int magic, size;
    struct sctx_info *info = (struct sctx_info *)&sc->sc_extcontext;

    while (1) {
        __get_user(magic, &info->magic);
        __get_user(size, &info->size);

        switch (magic) {
        case 0: /* END */
            goto done;
        case FPU_CTX_MAGIC:
            if (size < (sizeof(struct sctx_info) +
                        sizeof(struct target_fpu_context))) {
                goto invalid;
            }
            extctx->fpu.addr = (unsigned long)info;
            break;
        default:
            goto invalid;
        }
        info = (struct sctx_info *)((char *)info + size);
    }

done:
    return 0;
invalid:
    return -TARGET_EINVAL;
}

static int restore_sigframe(CPULoongArchState *env,
                            struct target_rt_sigframe *frame)
{
    int i;
    int ret = 0;
    struct extctx_layout extctx;
    struct target_sigcontext *sc = &frame->rs_uc.tuc_mcontext;

    memset(&extctx, 0, sizeof(struct extctx_layout));

    __get_user(extctx.flags, &sc->sc_flags);

    ret = parse_extcontext(sc, &extctx);
    if (ret < 0) {
        goto bad;
    }

    __get_user(env->pc, &sc->sc_pc);
    for (i = 1; i < 32; ++i) {
        __get_user(env->gpr[i], &sc->sc_regs[i]);
    }

    if (extctx.fpu.addr) {
        copy_fpu_from_sigframe(env, frame);
        restore_fp_status(env);
    }
bad:
    return ret;
}

/*
 * Determine which stack to use.
 */
static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPULoongArchState *env, size_t frame_size,
                              struct extctx_layout *extctx)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(env) - 32, ka);
    sp = (sp - frame_size) & ~15UL;

    return sp;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPULoongArchState *env)
{
    struct target_rt_sigframe *frame;
    struct extctx_layout extctx;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame), &extctx);
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    setup_extcontext(&extctx, frame_addr);

    tswap_siginfo(&frame->rs_info, info);

    __put_user(0, &frame->rs_uc.tuc_flags);
    __put_user(0, &frame->rs_uc.tuc_link);
    target_save_altstack(&frame->rs_uc.tuc_stack, env);

    setup_sigframe(env, frame, &extctx);

    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->rs_uc.tuc_sigmask.sig[i]);
    }

    env->gpr[4] = sig;
    env->gpr[5] = frame_addr + offsetof(struct target_rt_sigframe, rs_info);
    env->gpr[6] = frame_addr + offsetof(struct target_rt_sigframe, rs_uc);
    env->gpr[3] = frame_addr;
    env->gpr[1] = default_rt_sigreturn;

    env->pc = ka->_sa_handler;
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}

long do_rt_sigreturn(CPULoongArchState *env)
{
    int ret = 0;
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;

    frame_addr = env->gpr[3];
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    target_to_host_sigset(&blocked, &frame->rs_uc.tuc_sigmask);
    set_sigmask(&blocked);

    ret = restore_sigframe(env, frame);
    if (ret < 0) {
        goto badframe;
    }

    target_restore_altstack(&frame->rs_uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 8, 0);
    assert(tramp != NULL);

    __put_user(0x03822c0b, tramp + 0);  /* ori     a7, zero, 0x8b */
    __put_user(0x002b0000, tramp + 1);  /* syscall 0 */

    default_rt_sigreturn = sigtramp_page;
    unlock_user(tramp, sigtramp_page, 8);
}
