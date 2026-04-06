/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <zlib.h>

void compressBound_HTL(void **args, void *ret);
void compress2_HTL(void **args, void *ret);
void uncompress_HTL(void **args, void *ret);

void compressBound_HTL(void **args, void *ret)
{
    uLong sourceLen = *(uLong *)args[0];

    *(uLong *)ret = compressBound(sourceLen);
}

void compress2_HTL(void **args, void *ret)
{
    Bytef *dest = *(Bytef **)args[0];
    uLongf *destLen = *(uLongf **)args[1];
    const Bytef *source = *(const Bytef **)args[2];
    uLong sourceLen = *(uLong *)args[3];
    int level = *(int *)args[4];

    *(int *)ret = compress2(dest, destLen, source, sourceLen, level);
}

void uncompress_HTL(void **args, void *ret)
{
    Bytef *dest = *(Bytef **)args[0];
    uLongf *destLen = *(uLongf **)args[1];
    const Bytef *source = *(const Bytef **)args[2];
    uLong sourceLen = *(uLong *)args[3];

    *(int *)ret = uncompress(dest, destLen, source, sourceLen);
}
