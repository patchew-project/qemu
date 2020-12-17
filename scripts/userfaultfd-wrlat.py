#!/usr/bin/python
#
# userfaultfd-wrlat Summarize userfaultfd write fault latencies.
#                   Events are continuously accumulated for the
#                   run, while latency distribution histogram is
#                   dumped each 'interval' seconds.
#
#                   For Linux, uses BCC, eBPF.
#
# USAGE: userfaultfd-lat [interval [count]]
#
# Copyright Virtuozzo GmbH, 2020
#
# Authors:
#   Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from __future__ import print_function
from bcc import BPF
from ctypes import c_ushort, c_int, c_ulonglong
from time import sleep
from sys import argv

def usage():
    print("USAGE: %s [interval [count]]" % argv[0])
    exit()

# define BPF program
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <linux/mm.h>

/*
 * UFFD page fault event descriptor.
 * Used as a key to BPF_HASH table.
 */
struct ev_desc {
    u64 pid;
    u64 addr;
};

BPF_HASH(ev_start, struct ev_desc, u64);
BPF_HASH(ctx_handle_userfault, u64, u64);
BPF_HISTOGRAM(ev_delta_hist, u64);

/* Trace UFFD page fault start event. */
static void do_event_start(u64 pid, u64 address)
{
    struct ev_desc desc = { .pid = pid, .addr = address };
    u64 ts = bpf_ktime_get_ns();

    ev_start.insert(&desc, &ts);
}

/* Trace UFFD page fault end event. */
static void do_event_end(u64 pid, u64 address)
{
    struct ev_desc desc = { .pid = pid, .addr = address };
    u64 ts = bpf_ktime_get_ns();
    u64 *tsp;

    tsp = ev_start.lookup(&desc);
    if (tsp) {
        u64 delta = ts - (*tsp);
        /* Transform time delta to milliseconds */
        ev_delta_hist.increment(bpf_log2l(delta / 1000000));
        ev_start.delete(&desc);
    }
}

/* KPROBE for handle_userfault(). */
int probe_handle_userfault(struct pt_regs *ctx, struct vm_fault *vmf,
        unsigned long reason)
{
    /* Trace only UFFD write faults. */
    if (reason & VM_UFFD_WP) {
        u64 pid = (u32) bpf_get_current_pid_tgid();
        u64 addr = vmf->address;

        do_event_start(pid, addr);
        ctx_handle_userfault.update(&pid, &addr);
    }
    return 0;
}

/* KRETPROBE for handle_userfault(). */
int retprobe_handle_userfault(struct pt_regs *ctx)
{
    u64 pid = (u32) bpf_get_current_pid_tgid();
    u64 *addr_p;

    /*
     * Here we just ignore the return value. In case of spurious wakeup
     * or pending signal we'll still get (at least for v5.8.0 kernel) 
     * VM_FAULT_RETRY or (VM_FAULT_RETRY | VM_FAULT_MAJOR) here.
     * Anyhow, handle_userfault() would be re-entered if such case happens,
     * keeping initial timestamp unchanged for the faulting thread.  
     */
    addr_p = ctx_handle_userfault.lookup(&pid);
    if (addr_p) {
        do_event_end(pid, *addr_p);
        ctx_handle_userfault.delete(&pid);
    }
    return 0;
}
"""

# arguments
interval = 10
count = -1
if len(argv) > 1:
    try:
        interval = int(argv[1])
        if interval == 0:
            raise
        if len(argv) > 2:
            count = int(argv[2])
    except:    # also catches -h, --help
        usage()

# load BPF program
b = BPF(text=bpf_text)
# attach KRPOBEs
b.attach_kprobe(event="handle_userfault", fn_name="probe_handle_userfault")
b.attach_kretprobe(event="handle_userfault", fn_name="retprobe_handle_userfault")

# header
print("Tracing UFFD-WP write fault latency... Hit Ctrl-C to end.")

# output
loop = 0
do_exit = 0
while (1):
    if count > 0:
        loop += 1
        if loop > count:
            exit()
    try:
        sleep(interval)
    except KeyboardInterrupt:
        pass; do_exit = 1

    print()
    b["ev_delta_hist"].print_log2_hist("msecs")
    if do_exit:
        exit()
