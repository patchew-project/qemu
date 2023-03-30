/*
 * QEMU eBPF binary declaration routine.
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef EBPF_H
#define EBPF_H

void ebpf_register_binary_data(const char *id, const void * (*fn)(size_t *));
const void *ebpf_find_binary_by_id(const char *id, size_t *sz);

#define ebpf_binary_init(id, fn)                                           \
static void __attribute__((constructor)) ebpf_binary_init_ ## fn(void)     \
{                                                                          \
    ebpf_register_binary_data(id, fn);                                     \
}

#endif /* EBPF_H */
