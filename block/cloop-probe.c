#include "qemu/osdep.h"
#include "block/probe.h"

int cloop_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const char *magic_version_2_0 = "#!/bin/sh\n"
        "#V2.0 Format\n"
        "modprobe cloop file=$0 && mount -r -t iso9660 /dev/cloop $1\n";
    int length = strlen(magic_version_2_0);
    if (length > buf_size) {
        length = buf_size;
    }
    if (!memcmp(magic_version_2_0, buf, length)) {
        return 2;
    }
    return 0;
}
