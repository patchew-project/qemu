#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "parallels.h"

const char *bdrv_parallels_probe(const uint8_t *buf, int buf_size,
                                 const char *filename, int *score)
{
    const char *format = "parallels";
    const ParallelsHeader *ph = (const void *)buf;
    assert(score);
    *score = 0;

    if (buf_size < sizeof(ParallelsHeader)) {
        return format;
    }

    if ((!memcmp(ph->magic, HEADER_MAGIC, 16) ||
        !memcmp(ph->magic, HEADER_MAGIC2, 16)) &&
        (le32_to_cpu(ph->version) == HEADER_VERSION)) {
        *score = 100;
        return format;
    }

    return format;
}
