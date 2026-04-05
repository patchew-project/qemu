/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Darwin shim for <linux/types.h>
 *
 * The Linux UAPI headers that QEMU copies into linux-headers/ expect
 * these typedefs from <linux/types.h>.  Provide the small subset we
 * need so those headers parse on macOS.
 */
#ifndef COMPAT_LINUX_TYPES_H
#define COMPAT_LINUX_TYPES_H

#include <stdint.h>

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int8_t   __s8;
typedef int16_t  __s16;
typedef int32_t  __s32;
typedef int64_t  __s64;
typedef int64_t  loff_t;

typedef __u64 __aligned_u64 __attribute__((aligned(8)));

#endif /* COMPAT_LINUX_TYPES_H */
