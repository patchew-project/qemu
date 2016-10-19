#ifndef QEMU_TCMU_H
#define QEMU_TCMU_H

#include "qemu-common.h"

typedef struct TCMUExport TCMUExport;

void qemu_tcmu_start(const char *subtype, Error **errp);
TCMUExport *qemu_tcmu_export(BlockBackend *blk, bool writable, Error **errp);

#endif
