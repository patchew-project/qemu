#include "qemu/osdep.h"
#include "block/probe.h"

/*
 * Per the MS VHDX Specification, for every VHDX file:
 *      - The header section is fixed size - 1 MB
 *      - The header section is always the first "object"
 *      - The first 64KB of the header is the File Identifier
 *      - The first uint64 (8 bytes) is the VHDX Signature ("vhdxfile")
 *      - The following 512 bytes constitute a UTF-16 string identifiying the
 *        software that created the file, and is optional and diagnostic only.
 *
 *  Therefore, we probe by looking for the vhdxfile signature "vhdxfile"
 */
const char *bdrv_vhdx_probe(const uint8_t *buf, int buf_size,
                            const char *filename, int *score)
{
    const char *format = "vhdx";
    assert(score);

    if (buf_size >= 8 && !memcmp(buf, "vhdxfile", 8)) {
        *score = 100;
        return format;
    }
    *score = 0;
    return format;
}
