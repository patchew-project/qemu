#ifndef QEMU_USER_COMMON_H
#define QEMU_USER_COMMON_H

#include "cpu.h"
#include "exec/cpu_ldst.h"

/* user access */

#define VERIFY_READ 0
#define VERIFY_WRITE 1 /* implies read access */

static inline int access_ok(int type, abi_ulong addr, abi_ulong size)
{
    return page_check_range((target_ulong)addr, size,
                            (type == VERIFY_READ) ? PAGE_READ : (PAGE_READ | PAGE_WRITE)) == 0;
}

/* NOTE __get_user and __put_user use host pointers and don't check access.
   These are usually used to access struct data members once the struct has
   been locked - usually with lock_user_struct.  */

/* Tricky points:
   - Use __builtin_choose_expr to avoid type promotion from ?:,
   - Invalid sizes result in a compile time error stemming from
     the fact that abort has no parameters.
   - It's easier to use the endian-specific unaligned load/store
     functions than host-endian unaligned load/store plus tswapN.  */

#define __put_user_e(x, hptr, e)                                        \
  (__builtin_choose_expr(sizeof(*(hptr)) == 1, stb_p,                   \
   __builtin_choose_expr(sizeof(*(hptr)) == 2, stw_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 4, stl_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 8, stq_##e##_p, abort))))   \
     ((hptr), (x)), (void)0)

#define __get_user_e(x, hptr, e)                                        \
  ((x) = (typeof(*hptr))(                                               \
   __builtin_choose_expr(sizeof(*(hptr)) == 1, ldub_p,                  \
   __builtin_choose_expr(sizeof(*(hptr)) == 2, lduw_##e##_p,            \
   __builtin_choose_expr(sizeof(*(hptr)) == 4, ldl_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 8, ldq_##e##_p, abort))))   \
     (hptr)), (void)0)

#ifdef TARGET_WORDS_BIGENDIAN
# define __put_user(x, hptr)  __put_user_e(x, hptr, be)
# define __get_user(x, hptr)  __get_user_e(x, hptr, be)
#else
# define __put_user(x, hptr)  __put_user_e(x, hptr, le)
# define __get_user(x, hptr)  __get_user_e(x, hptr, le)
#endif

/* put_user()/get_user() take a guest address and check access */
/* These are usually used to access an atomic data type, such as an int,
 * that has been passed by address.  These internally perform locking
 * and unlocking on the data type.
 */
#define put_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_WRITE, __gaddr, sizeof(target_type), 0))) { \
        __put_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, sizeof(target_type));		\
    } else								\
        __ret = -TARGET_EFAULT;						\
    __ret;								\
})

#define get_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_READ, __gaddr, sizeof(target_type), 1))) { \
        __get_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, 0);				\
    } else {								\
        /* avoid warning */						\
        (x) = 0;							\
        __ret = -TARGET_EFAULT;						\
    }									\
    __ret;								\
})

#define put_user_ual(x, gaddr) put_user((x), (gaddr), abi_ulong)
#define put_user_sal(x, gaddr) put_user((x), (gaddr), abi_long)
#define put_user_u64(x, gaddr) put_user((x), (gaddr), uint64_t)
#define put_user_s64(x, gaddr) put_user((x), (gaddr), int64_t)
#define put_user_u32(x, gaddr) put_user((x), (gaddr), uint32_t)
#define put_user_s32(x, gaddr) put_user((x), (gaddr), int32_t)
#define put_user_u16(x, gaddr) put_user((x), (gaddr), uint16_t)
#define put_user_s16(x, gaddr) put_user((x), (gaddr), int16_t)
#define put_user_u8(x, gaddr)  put_user((x), (gaddr), uint8_t)
#define put_user_s8(x, gaddr)  put_user((x), (gaddr), int8_t)

#define get_user_ual(x, gaddr) get_user((x), (gaddr), abi_ulong)
#define get_user_sal(x, gaddr) get_user((x), (gaddr), abi_long)
#define get_user_u64(x, gaddr) get_user((x), (gaddr), uint64_t)
#define get_user_s64(x, gaddr) get_user((x), (gaddr), int64_t)
#define get_user_u32(x, gaddr) get_user((x), (gaddr), uint32_t)
#define get_user_s32(x, gaddr) get_user((x), (gaddr), int32_t)
#define get_user_u16(x, gaddr) get_user((x), (gaddr), uint16_t)
#define get_user_s16(x, gaddr) get_user((x), (gaddr), int16_t)
#define get_user_u8(x, gaddr)  get_user((x), (gaddr), uint8_t)
#define get_user_s8(x, gaddr)  get_user((x), (gaddr), int8_t)

/* copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
abi_long copy_from_user(void *hptr, abi_ulong gaddr, size_t len);
abi_long copy_to_user(abi_ulong gaddr, void *hptr, size_t len);

/* Functions for accessing guest memory.  The tget and tput functions
   read/write single values, byteswapping as necessary.  The lock_user function
   gets a pointer to a contiguous area of guest memory, but does not perform
   any byteswapping.  lock_user may return either a pointer to the guest
   memory, or a temporary buffer.  */

/* Lock an area of guest memory into the host.  If copy is true then the
   host area will have the same contents as the guest.  */
static inline void *lock_user(int type, abi_ulong guest_addr, long len, int copy)
{
    if (!access_ok(type, guest_addr, len))
        return NULL;
#ifdef DEBUG_REMAP
    {
        void *addr;
        addr = g_malloc(len);
        if (copy)
            memcpy(addr, g2h(guest_addr), len);
        else
            memset(addr, 0, len);
        return addr;
    }
#else
    return g2h(guest_addr);
#endif
}

/* Unlock an area of guest memory.  The first LEN bytes must be
   flushed back to guest memory. host_ptr = NULL is explicitly
   allowed and does nothing. */
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               long len)
{

#ifdef DEBUG_REMAP
    if (!host_ptr)
        return;
    if (host_ptr == g2h(guest_addr))
        return;
    if (len > 0)
        memcpy(g2h(guest_addr), host_ptr, len);
    g_free(host_ptr);
#endif
}

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error. */
abi_long target_strlen(abi_ulong gaddr);

/* Like lock_user but for null terminated strings.  */
static inline void *lock_user_string(abi_ulong guest_addr)
{
    abi_long len;
    len = target_strlen(guest_addr);
    if (len < 0)
        return NULL;
    return lock_user(VERIFY_READ, guest_addr, (long)(len + 1), 1);
}

/* Helper macros for locking/unlocking a target struct.  */
#define lock_user_struct(type, host_ptr, guest_addr, copy)	\
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)		\
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#endif /* QEMU_USER_COMMON_H */
