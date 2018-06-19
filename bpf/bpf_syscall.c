/*
 * BPF syscalls
 *
 * Based on bpf-syscall.c from iovisor/ply
 *
 * Authors:
 *  Sameeh Jubran <sameeh@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <string.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <linux/version.h>
#include <sys/syscall.h>

#include "bpf/bpf_syscall.h"

static __u64 ptr_to_u64(const void *ptr)
{
        return (__u64) (unsigned long) ptr;
}

int bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns,
                int insn_cnt, const char *license, unsigned kern_version,
                int log_level, char *bpf_log_buf, unsigned log_buf_size)
{
    union bpf_attr attr;

    /* required since the kernel checks that unused fields and pad
     * bytes are zeroed */
    memset(&attr, 0, sizeof(attr));

    attr.kern_version = kern_version;
    attr.prog_type    = type;
    attr.insns        = ptr_to_u64(insns);
    attr.insn_cnt     = insn_cnt;
    attr.license      = ptr_to_u64(license);
    attr.log_buf      = ptr_to_u64(bpf_log_buf);
    attr.log_size     = log_buf_size;
    attr.log_level    = log_level;

    return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}

int bpf_map_create(enum bpf_map_type type, int key_size, int val_size,
                int entries)
{
    union bpf_attr attr;

    /* required since the kernel checks that unused fields and pad
     * bytes are zeroed */
    memset(&attr, 0, sizeof(attr));

    attr.map_type = type;
    attr.key_size = key_size;
    attr.value_size = val_size;
    attr.max_entries = entries;

    return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}


static int bpf_map_operation(enum bpf_cmd cmd, int fd,
              void *key, void *val_or_next, int flags)
{
    union bpf_attr attr =  {
        .map_fd = fd,
        .key = ptr_to_u64(key),
        .value = ptr_to_u64(val_or_next),
        .flags = flags,
    };

    return syscall(__NR_bpf, cmd, &attr, sizeof(attr));
}

int bpf_map_lookup(int fd, void *key, void *val)
{
    return bpf_map_operation(BPF_MAP_LOOKUP_ELEM, fd, key, val, 0);
}

int bpf_map_update(int fd, void *key, void *val, int flags)
{
    return bpf_map_operation(BPF_MAP_UPDATE_ELEM, fd, key, val, flags);
}

int bpf_map_delete(int fd, void *key)
{
    return bpf_map_operation(BPF_MAP_DELETE_ELEM, fd, key, NULL, 0);
}

int bpf_map_next(int fd, void *key, void *next_key)
{
    return bpf_map_operation(BPF_MAP_GET_NEXT_KEY, fd, key, next_key, 0);
}
