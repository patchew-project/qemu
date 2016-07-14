#include "qemu/osdep.h"
#include "block/probe.h"

int raw_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    /* smallest possible positive score so that raw is used if and only if no
     * other block driver works
     */
    return 1;
}
