/*
 * Definitions of Linux-specific mmap flags.
 *
 * Copyright Intel Corporation, 2018
 *
 * Author: Haozhong Zhang <haozhong.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

/*
 * MAP_SHARED_VALIDATE and MAP_SYNC are introduced in Linux kernel
 * 4.15, so they may not be defined when compiling on older kernels.
 */
#ifdef CONFIG_LINUX

#include <sys/mman.h>

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE   0x3
#endif

#ifndef MAP_SYNC
#define MAP_SYNC              0x80000
#endif

/* MAP_SYNC is only available with MAP_SHARED_VALIDATE. */
#define MAP_SYNC_FLAGS (MAP_SYNC | MAP_SHARED_VALIDATE)

#else  /* !CONFIG_LINUX */

#define MAP_SHARED_VALIDATE   0x0
#define MAP_SYNC              0x0

#define QEMU_HAS_MAP_SYNC     false
#define MAP_SYNC_FLAGS 0

#endif /* CONFIG_LINUX */

#endif /* !_LINUX_MMAN_H */
