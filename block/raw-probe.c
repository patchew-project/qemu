#include "qemu/osdep.h"
#include "block/probe.h"

const char *bdrv_raw_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score)
{
    const char *format = "raw";
    assert(score);
    /* smallest possible positive score so that raw is used if and only if no
     * other block driver works
     */
    *score = 1;
    return format;
}
