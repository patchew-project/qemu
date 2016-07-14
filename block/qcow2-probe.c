#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "qcow2.h"

const char *bdrv_qcow2_probe(const uint8_t *buf, int buf_size,
                             const char *filename, int *score)
{
    const char *format = "qcow2";
    const QCowHeader *cow_header = (const void *)buf;
    assert(score);

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) >= 2) {
        *score = 100;
        return format;
    }

    *score = 0;
    return format;
}
