#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "vdi.h"

const char *bdrv_vdi_probe(const uint8_t *buf, int buf_size,
                           const char *filename, int *score)
{
    const char *format = "vdi";
    const VdiHeader *header = (const VdiHeader *)buf;
    assert(score);
    *score = 0;

    logout("\n");

    if (buf_size < sizeof(*header)) {
        /* Header too small, no VDI. */
    } else if (le32_to_cpu(header->signature) == VDI_SIGNATURE) {
        *score = 100;
    }

    if (*score == 0) {
        logout("no vdi image\n");
    } else {
        logout("%s", header->text);
    }

    return format;
}
