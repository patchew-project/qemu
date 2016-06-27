#include "tcg/tcg.h"

static inline DATA_TYPE
glue(glue(glue(cpu_cmpxchg, SUFFIX), MEMSUFFIX),
     _ra)(CPUArchState *env, target_ulong ptr, DATA_TYPE old, DATA_TYPE new,
          uintptr_t ra)
{
    DATA_TYPE *hostaddr = g2h(ptr);

    return atomic_cmpxchg(hostaddr, old, new);
}

/* define cmpxchgo once ldq and stq have been defined */
#if DATA_SIZE == 8
/* returns true on success, false on failure */
static inline bool
glue(glue(cpu_cmpxchgo, MEMSUFFIX),
     _ra)(CPUArchState *env, target_ulong ptr, uint64_t *old_lo,
          uint64_t *old_hi, uint64_t new_lo, uint64_t new_hi, uintptr_t retaddr)
{
    uint64_t *hostaddr = g2h(ptr);
    uint64_t orig_lo, orig_hi;
    bool ret = true;

    tcg_cmpxchg_lock(ptr);
    orig_lo = *hostaddr;
    orig_hi = *(hostaddr + 1);
    if (orig_lo == *old_lo && orig_hi == *old_hi) {
        *hostaddr = new_lo;
        *(hostaddr + 1) = new_hi;
    } else {
        *old_lo = orig_lo;
        *old_hi = orig_hi;
        ret = false;
    }
    tcg_cmpxchg_unlock();
    return ret;
}
#endif

#define GEN_ATOMIC_HELPER(NAME)                                             \
static inline DATA_TYPE                                                     \
glue(glue(glue(glue(cpu_atomic_, NAME), SUFFIX), MEMSUFFIX),                \
     _ra)(CPUArchState *env, target_ulong ptr, DATA_TYPE val, uintptr_t ra) \
{                                                                           \
    DATA_TYPE *hostaddr = g2h(ptr);                                         \
                                                                            \
    return glue(atomic_, NAME)(hostaddr, val);                              \
}

GEN_ATOMIC_HELPER(fetch_add)
GEN_ATOMIC_HELPER(fetch_sub)
GEN_ATOMIC_HELPER(fetch_and)
GEN_ATOMIC_HELPER(fetch_or)
GEN_ATOMIC_HELPER(fetch_xor)

GEN_ATOMIC_HELPER(add_fetch)
GEN_ATOMIC_HELPER(sub_fetch)
GEN_ATOMIC_HELPER(and_fetch)
GEN_ATOMIC_HELPER(or_fetch)
GEN_ATOMIC_HELPER(xor_fetch)

GEN_ATOMIC_HELPER(xchg)
#undef GEN_ATOMIC_HELPER
