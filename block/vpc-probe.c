#include "qemu/osdep.h"
#include "block/probe.h"

const char *bdrv_vpc_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score)
{
    const char *format = "vpc";
    assert(score);
    if (buf_size >= 8 && !strncmp((char *)buf, "conectix", 8)) {
        *score = 100;
        return format;
    }

    *score = 0;
    return format;
}
