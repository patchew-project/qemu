/*
 *  native function call helpers
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#if defined(CONFIG_USER_ONLY)  && defined(CONFIG_USER_NATIVE_CALL)

#define NATIVE_FN_W_3W()                   \
    target_ulong arg0, arg1, arg2;         \
    arg0 = env->active_tc.gpr[4]; /*"a0"*/ \
    arg1 = env->active_tc.gpr[5]; /*"a1"*/ \
    arg2 = env->active_tc.gpr[6]; /*"a2"*/

void helper_native_memcpy(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    void *ret;
    void *dest = g2h(cs, arg0);
    void *src = g2h(cs, arg1);
    size_t n = (size_t)arg2;
    ret = memcpy(dest, src, n);
    env->active_tc.gpr[2] = (target_ulong)h2g(ret);
}

void helper_native_memcmp(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    int ret;
    void *s1 = g2h(cs, arg0);
    void *s2 = g2h(cs, arg1);
    size_t n = (size_t)arg2;
    ret = memcmp(s1, s2, n);
    env->active_tc.gpr[2] = ret;
}

void helper_native_memset(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    NATIVE_FN_W_3W();
    void *ret;
    void *s = g2h(cs, arg0);
    int c = (int)arg1;
    size_t n = (size_t)arg2;
    ret = memset(s, c, n);
    env->active_tc.gpr[2] = (target_ulong)h2g(ret);
}

#endif
