#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "vdi.h"

int vdi_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const VdiHeader *header = (const VdiHeader *)buf;
    int ret = 0;

    logout("\n");

    if (buf_size < sizeof(*header)) {
        /* Header too small, no VDI. */
    } else if (le32_to_cpu(header->signature) == VDI_SIGNATURE) {
        ret = 100;
    }

    if (ret == 0) {
        logout("no vdi image\n");
    } else {
        logout("%s", header->text);
    }

    return ret;
}
