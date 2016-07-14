#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "qed.h"

const char *bdrv_qed_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score)
{
    const char *format = "qed";
    const QEDHeader *header = (const QEDHeader *)buf;
    assert(score);
    *score = 0;

    if (buf_size < sizeof(*header)) {
        return format;
    }

    if (le32_to_cpu(header->magic) != QED_MAGIC) {
        return format;
    }

    *score = 100;
    return format;
}
