#include "qemu/osdep.h"
#include "block/probe.h"

const char *bdrv_cloop_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score)
{
    const char *format = "cloop";
    const char *magic_version_2_0 = "#!/bin/sh\n"
        "#V2.0 Format\n"
        "modprobe cloop file=$0 && mount -r -t iso9660 /dev/cloop $1\n";
    int length = strlen(magic_version_2_0);
    assert(score);
    if (length > buf_size) {
        length = buf_size;
    }
    if (!memcmp(magic_version_2_0, buf, length)) {
        *score = 2;
        return format;
    }
    *score = 0;
    return format;
}
