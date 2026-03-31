/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CONTRIB_PLUGINS_SYSCALL_FILTER_CALLBACK_QSORT_EXAMPLE_CALLBACK_QSORT_H
#define CONTRIB_PLUGINS_SYSCALL_FILTER_CALLBACK_QSORT_EXAMPLE_CALLBACK_QSORT_H

#include <stddef.h>
typedef int (*callback_qsort_cmp_fn)(const void *lhs, const void *rhs);

int callback_qsort(void *base, size_t nmemb, size_t size,
                   callback_qsort_cmp_fn cmp);

#endif
