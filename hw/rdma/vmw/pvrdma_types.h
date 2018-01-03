/*
 * QEMU VMWARE paravirtual RDMA interface definitions
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PVRDMA_TYPES_H
#define PVRDMA_TYPES_H

/* TDOD: All defs here should be removed !!! */

#include <stdbool.h>
#include <stdint.h>
#include <asm-generic/int-ll64.h>
#include <include/sysemu/dma.h>
#include <linux/types.h>

typedef unsigned char uint8_t;
typedef uint8_t           u8;
typedef u8                __u8;
typedef unsigned short    u16;
typedef u16               __u16;
typedef uint32_t          u32;
typedef u32               __u32;
typedef int32_t           __s32;
typedef uint64_t          u64;
typedef __u64 __bitwise   __be64;

#endif
