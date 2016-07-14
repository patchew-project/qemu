#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "parallels.h"

int parallels_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const ParallelsHeader *ph = (const void *)buf;

    if (buf_size < sizeof(ParallelsHeader)) {
        return 0;
    }

    if ((!memcmp(ph->magic, HEADER_MAGIC, 16) ||
           !memcmp(ph->magic, HEADER_MAGIC2, 16)) &&
           (le32_to_cpu(ph->version) == HEADER_VERSION)) {
        return 100;
    }

    return 0;
}
