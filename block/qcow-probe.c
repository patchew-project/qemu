#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "qcow.h"

const char *bdrv_qcow_probe(const uint8_t *buf, int buf_size,
                            const char *filename, int *score)
{
    const char *format = "qcow";
    const QCowHeader *cow_header = (const void *)buf;
    assert(score);

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) == QCOW_VERSION) {
        *score = 100;
        return format;
    }

    *score = 0;
    return format;
}
