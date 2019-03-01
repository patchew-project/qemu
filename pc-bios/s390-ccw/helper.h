#ifndef S390_CCW_HELPER_H
#define S390_CCW_HELPER_H

/* Avoids compiler warnings when casting a pointer to a u32 */
static inline uint32_t ptr2u32(void *ptr)
{
    return (uint32_t)(uint64_t)ptr;
}

/* Avoids compiler warnings when casting a u32 to a pointer */
static inline void *u32toptr(uint32_t n)
{
    return (void *)(uint64_t)n;
}

#endif
