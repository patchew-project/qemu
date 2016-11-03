/*
 * Helper routines to provide target memory access for ARM semihosting
 * syscalls in system emulation mode.
 *
 * Copyright (c) 2007 CodeSourcery, (c) 2016 Mentor Graphics
 *
 * This code is licensed under the GPL
 */

#ifndef SOFTMMU_ARM_SEMI_H
#define SOFTMMU_ARM_SEMI_H 1

/* In BE32 system mode, the CPU-specific memory_rw_debug method will arrange to
 * perform byteswapping on the target memory, so that it appears to the host as
 * it appears to the emulated CPU.  Memory is read verbatim in BE8 mode.  (In
 * other words, this function arranges so that BUF has the same format in both
 * BE8 and BE32 system mode.)
 */

static inline int armsemi_memory_rw_debug(CPUState *cpu, target_ulong addr,
                                          uint8_t *buf, int len, bool is_write)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

/* In big-endian mode (either BE8 or BE32), values larger than a byte will be
 * transferred to/from memory in big-endian format.  Assuming we're on a
 * little-endian host machine, such values will need to be byteswapped before
 * and after the host processes them.
 *
 * This means that byteswapping will occur *twice* in BE32 mode for
 * halfword/word reads/writes.
 */

static inline bool arm_bswap_needed(CPUARMState *env)
{
#ifdef HOST_WORDS_BIGENDIAN
#error HOST_WORDS_BIGENDIAN is not supported for ARM semihosting at the moment.
#else
    return arm_sctlr_b(env) || (env->uncached_cpsr & CPSR_E) != 0;
#endif
}

static inline uint64_t softmmu_tget64(CPUArchState *env, target_ulong addr)
{
    uint64_t val;

    armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, (uint8_t *)&val, 8, 0);
    if (arm_bswap_needed(env)) {
        return bswap64(val);
    } else {
        return val;
    }
}

static inline uint32_t softmmu_tget32(CPUArchState *env, target_ulong addr)
{
    uint32_t val;

    armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, (uint8_t *)&val, 4, 0);
    if (arm_bswap_needed(env)) {
        return bswap32(val);
    } else {
        return val;
    }
}

static inline uint32_t softmmu_tget8(CPUArchState *env, target_ulong addr)
{
    uint8_t val;
    armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, &val, 1, 0);
    return val;
}

#define get_user_u64(arg, p) ({ arg = softmmu_tget64(env, p); 0; })
#define get_user_u32(arg, p) ({ arg = softmmu_tget32(env, p) ; 0; })
#define get_user_u8(arg, p) ({ arg = softmmu_tget8(env, p) ; 0; })
#define get_user_ual(arg, p) get_user_u32(arg, p)

static inline void softmmu_tput64(CPUArchState *env,
                                  target_ulong addr, uint64_t val)
{
    if (arm_bswap_needed(env)) {
        val = bswap64(val);
    }
    cpu_memory_rw_debug(ENV_GET_CPU(env), addr, (uint8_t *)&val, 8, 1);
}

static inline void softmmu_tput32(CPUArchState *env,
                                  target_ulong addr, uint32_t val)
{
    if (arm_bswap_needed(env)) {
        val = bswap32(val);
    }
    armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, (uint8_t *)&val, 4, 1);
}
#define put_user_u64(arg, p) ({ softmmu_tput64(env, p, arg) ; 0; })
#define put_user_u32(arg, p) ({ softmmu_tput32(env, p, arg) ; 0; })
#define put_user_ual(arg, p) put_user_u32(arg, p)

static void *softmmu_lock_user(CPUArchState *env,
                               target_ulong addr, target_ulong len, int copy)
{
    uint8_t *p;
    /* TODO: Make this something that isn't fixed size.  */
    p = malloc(len);
    if (p && copy) {
        armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, p, len, 0);
    }
    return p;
}
#define lock_user(type, p, len, copy) softmmu_lock_user(env, p, len, copy)
static char *softmmu_lock_user_string(CPUArchState *env, target_ulong addr)
{
    char *p;
    char *s;
    uint8_t c;
    /* TODO: Make this something that isn't fixed size.  */
    s = p = malloc(1024);
    if (!s) {
        return NULL;
    }
    do {
        armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, &c, 1, 0);
        addr++;
        *(p++) = c;
    } while (c);
    return s;
}
#define lock_user_string(p) softmmu_lock_user_string(env, p)
static void softmmu_unlock_user(CPUArchState *env, void *p, target_ulong addr,
                                target_ulong len)
{
    uint8_t *pc = p;
    if (len) {
        armsemi_memory_rw_debug(ENV_GET_CPU(env), addr, p, len, 1);
    }
    free(p);
}

#define unlock_user(s, args, len) softmmu_unlock_user(env, s, args, len)

#endif
