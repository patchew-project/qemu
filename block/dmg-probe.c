#include "qemu/osdep.h"
#include "block/probe.h"

const char *bdrv_dmg_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score)
{
    const char *format = "dmg";
    int len;
    assert(score);
    *score = 0;

    if (!filename) {
        return format;
    }

    len = strlen(filename);
    if (len > 4 && !strcmp(filename + len - 4, ".dmg")) {
        *score = 2;
        return format;
    }
    return format;
}
