/*
 * BPF syscalls
 *
 * Based on bpf-syscall.h from iovisor/ply
 *
 * Authors:
 *  Sameeh Jubran <sameeh@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef BPF_SYSCALL_H
#define BPF_SYSCALL_H

#include <unistd.h>

#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <linux/version.h>

int bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns,
                int insn_cnt, const char *license, unsigned kern_version,
                int log_level, char *bpf_log_buf, unsigned log_buf_size);

int bpf_map_create(enum bpf_map_type type, int key_size, int val_size,
                int entries);

int bpf_map_lookup(int fd, void *key, void *val);
int bpf_map_update(int fd, void *key, void *val, int flags);
int bpf_map_delete(int fd, void *key);
int bpf_map_next(int fd, void *key, void *next_key);

#endif
