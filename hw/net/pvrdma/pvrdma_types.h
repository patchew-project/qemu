/*
 * QEMU VMWARE paravirtual RDMA interface definitions
 *
 * Developed by Oracle & Redhat
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

#include <stdint.h>
#include <asm-generic/int-ll64.h>

typedef unsigned char uint8_t;
typedef uint64_t dma_addr_t;

typedef uint8_t        __u8;
typedef uint8_t        u8;
typedef unsigned short __u16;
typedef unsigned short u16;
typedef uint64_t       u64;
typedef uint32_t       u32;
typedef uint32_t       __u32;
typedef int32_t       __s32;
#define __bitwise
typedef __u64 __bitwise __be64;

#endif
