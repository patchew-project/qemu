#include "qemu/osdep.h"
#include "block/block_int.h"
#include "block/probe.h"
#include "bochs.h"

int bochs_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const struct bochs_header *bochs = (const void *)buf;

    if (buf_size < HEADER_SIZE)
	return 0;

    if (!strcmp(bochs->magic, HEADER_MAGIC) &&
	!strcmp(bochs->type, REDOLOG_TYPE) &&
	!strcmp(bochs->subtype, GROWING_TYPE) &&
	((le32_to_cpu(bochs->version) == HEADER_VERSION) ||
	(le32_to_cpu(bochs->version) == HEADER_V1)))
	return 100;

    return 0;
}
