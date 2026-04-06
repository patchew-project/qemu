/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <zlib.h>

#include "passthrough-gtl.h"

enum ZlibThunkIndex {
    ZLIB_THUNK_COMPRESS_BOUND,
    ZLIB_THUNK_COMPRESS2,
    ZLIB_THUNK_UNCOMPRESS,
    ZLIB_THUNK_COUNT,
};

static const char gtl_library_name[] = "libz.so";
static uint64_t gtl_htl_handle;
static PassthroughThunkEntry gtl_entries[ZLIB_THUNK_COUNT];

__attribute__((constructor))
static void passthrough_gtl_init(void)
{
    gtl_htl_handle = passthrough_load_htl(gtl_library_name);
    gtl_entries[ZLIB_THUNK_COMPRESS_BOUND] =
        (PassthroughThunkEntry)(uintptr_t)
        passthrough_dlsym(gtl_htl_handle, "compressBound_HTL");
    gtl_entries[ZLIB_THUNK_COMPRESS2] = (PassthroughThunkEntry)(uintptr_t)
        passthrough_dlsym(gtl_htl_handle, "compress2_HTL");
    gtl_entries[ZLIB_THUNK_UNCOMPRESS] = (PassthroughThunkEntry)(uintptr_t)
        passthrough_dlsym(gtl_htl_handle, "uncompress_HTL");
}

__attribute__((destructor))
static void passthrough_gtl_fini(void)
{
    if (gtl_htl_handle != 0) {
        passthrough_close_htl(gtl_htl_handle);
        gtl_htl_handle = 0;
    }
    gtl_entries[ZLIB_THUNK_COMPRESS_BOUND] = NULL;
    gtl_entries[ZLIB_THUNK_COMPRESS2] = NULL;
    gtl_entries[ZLIB_THUNK_UNCOMPRESS] = NULL;
}

uLong compressBound(uLong sourceLen)
{
    void *args[] = { &sourceLen };
    uLong ret = 0;

    passthrough_invoke(gtl_entries[ZLIB_THUNK_COMPRESS_BOUND], args, &ret);
    return ret;
}

int compress2(Bytef *dest, uLongf *destLen, const Bytef *source,
              uLong sourceLen, int level)
{
    void *args[] = { &dest, &destLen, &source, &sourceLen, &level };
    int ret = Z_ERRNO;

    passthrough_invoke(gtl_entries[ZLIB_THUNK_COMPRESS2], args, &ret);
    return ret;
}

int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source,
               uLong sourceLen)
{
    void *args[] = { &dest, &destLen, &source, &sourceLen };
    int ret = Z_ERRNO;

    passthrough_invoke(gtl_entries[ZLIB_THUNK_UNCOMPRESS], args, &ret);
    return ret;
}
