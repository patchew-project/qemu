#include "qemu/osdep.h"
#include "block/probe.h"

int dmg_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    int len;

    if (!filename) {
        return 0;
    }

    len = strlen(filename);
    if (len > 4 && !strcmp(filename + len - 4, ".dmg")) {
        return 2;
    }
    return 0;
}
