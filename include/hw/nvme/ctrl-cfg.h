/*
 * QEMU NVM Express Virtual Dynamic Namespace Management
 * Common configuration handling for qemu-img tool and and qemu-system-xx
 *
 *
 * Copyright (c) 2022 Solidigm
 *
 * Authors:
 *  Michael Kropaczek      <michael.kropaczek@solidigm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef CTRL_CFG_DEF
#define NVME_STR_(s) #s
#define NVME_STRINGIFY(s) NVME_STR_(s)
#define NVME_MAX_NAMESPACES  256
#define NVME_MAX_CONTROLLERS 256
#else
CTRL_CFG_DEF(int, "tnvmcap", int128_get64(tnvmcap128), tnvmcap64)
CTRL_CFG_DEF(int, "unvmcap", int128_get64(unvmcap128), unvmcap64)
#endif
