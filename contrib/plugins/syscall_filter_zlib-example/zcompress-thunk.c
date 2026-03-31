/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <unistd.h>

#include "zcompress-thunk.h"

#define ZLIB_COMPRESS_MAGIC_SYSCALL 4096
#define ZLIB_COMPRESS_OP_BOUND 1
#define ZLIB_COMPRESS_OP_COMPRESS2 2
#define ZLIB_COMPRESS_OP_UNCOMPRESS 3

size_t zcompress_compress_bound(size_t source_len)
{
    return (size_t)syscall(ZLIB_COMPRESS_MAGIC_SYSCALL,
                           ZLIB_COMPRESS_OP_BOUND, source_len);
}

int zcompress_compress(const void *source, size_t source_len,
                       void *dest, size_t *dest_len, int level)
{
    return (int)syscall(ZLIB_COMPRESS_MAGIC_SYSCALL,
                        ZLIB_COMPRESS_OP_COMPRESS2,
                        source, source_len, dest, dest_len, level);
}

int zcompress_uncompress(const void *source, size_t source_len,
                         void *dest, size_t *dest_len)
{
    return (int)syscall(ZLIB_COMPRESS_MAGIC_SYSCALL,
                        ZLIB_COMPRESS_OP_UNCOMPRESS,
                        source, source_len, dest, dest_len);
}
