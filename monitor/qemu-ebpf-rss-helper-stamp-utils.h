/*
 * QEMU helper stamp check utils.
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef QEMU_QEMU_HELPER_STAMP_UTILS_H
#define QEMU_QEMU_HELPER_STAMP_UTILS_H

#include "qemu-ebpf-rss-helper-stamp.h" /* generated stamp per build */

#define QEMU_EBPF_RSS_HELPER_STAMP_STR     stringify(QEMU_EBPF_RSS_HELPER_STAMP)

#define QEMU_DEFAULT_EBPF_RSS_HELPER_BIN_NAME "qemu-ebpf-rss-helper"

/**
 * Trying to find the helper with a valid stamp in HELPERDIR
 * or next to the QEMU binary.
 * @return path to the eBPF RSS helper bin or NULL(helper not found).
 */
char *qemu_find_default_ebpf_rss_helper(void);

/**
 * Check the helper by the suggested path. The helper should have a valid stamp.
 * @param path - it can be either a file or directory path.
 * For the file - checks the stamp of the file.
 * For the directory - looks for QEMU_DEFAULT_EBPF_RSS_HELPER_BIN_NAME
 * and checks the stamp of that file.
 * @return path to valid eBPF RSS helper bin or NULL.
 */
char *qemu_check_suggested_ebpf_rss_helper(const char *path);

#endif /* QEMU_QEMU_HELPER_STAMP_UTILS_H */
