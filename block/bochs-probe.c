#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "bochs.h"

const char *bdrv_bochs_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score)
{
    const char *format = "bochs";
    const struct bochs_header *bochs = (const void *)buf;
    assert(score);
    *score = 0;

    if (buf_size < HEADER_SIZE) {
        return format;
    }

    if (!strcmp(bochs->magic, HEADER_MAGIC) &&
        !strcmp(bochs->type, REDOLOG_TYPE) &&
        !strcmp(bochs->subtype, GROWING_TYPE) &&
        ((le32_to_cpu(bochs->version) == HEADER_VERSION) ||
        (le32_to_cpu(bochs->version) == HEADER_V1))) {
        *score = 100;
        return format;
    }

    return format;
}
