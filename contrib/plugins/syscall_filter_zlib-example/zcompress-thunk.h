/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CONTRIB_PLUGINS_SYSCALL_FILTER_ZLIB_EXAMPLE_ZCOMPRESS_THUNK_H
#define CONTRIB_PLUGINS_SYSCALL_FILTER_ZLIB_EXAMPLE_ZCOMPRESS_THUNK_H

#include <stddef.h>

size_t zcompress_compress_bound(size_t source_len);
int zcompress_compress(const void *source, size_t source_len,
                       void *dest, size_t *dest_len, int level);
int zcompress_uncompress(const void *source, size_t source_len,
                         void *dest, size_t *dest_len);

#endif
