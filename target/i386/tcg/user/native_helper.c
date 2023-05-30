/*
 *  native function call helpers
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "tcg/helper-tcg.h"
#include "tcg/seg_helper.h"

#ifdef TARGET_X86_64
#define NATIVE_FN_W_3W()           \
    target_ulong arg0, arg1, arg2; \
    arg0 = env->regs[R_EDI];       \
    arg1 = env->regs[R_ESI];       \
    arg2 = env->regs[R_EDX];
#else
/* linux x86 has several calling conventions. The following implementation
   is for the most commonly used cdecl calling convention. */
#define NATIVE_FN_W_3W()                                   \
    target_ulong arg0, arg1, arg2;                         \
    arg0 = *(target_ulong *)g2h(cs, env->regs[R_ESP] + 4); \
    arg1 = *(target_ulong *)g2h(cs, env->regs[R_ESP] + 8); \
    arg2 = *(target_ulong *)g2h(cs, env->regs[R_ESP] + 12);
#endif

void helper_native_memcpy(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    void *ret;
    void *dest = g2h(cs, arg0);
    void *src = g2h(cs, arg1);
    size_t n = (size_t)arg2;
    ret = memcpy(dest, src, n);
    env->regs[R_EAX] = (target_ulong)h2g(ret);
}

void helper_native_memcmp(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    int ret;
    void *s1 = g2h(cs, arg0);
    void *s2 = g2h(cs, arg1);
    size_t n = (size_t)arg2;
    ret = memcmp(s1, s2, n);
    env->regs[R_EAX] = ret;
}

void helper_native_memset(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    void *ret;
    void *s = g2h(cs, arg0);
    int c = (int)arg1;
    size_t n = (size_t)arg2;
    ret = memset(s, c, n);
    env->regs[R_EAX] = (target_ulong)h2g(ret);
}
