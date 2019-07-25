#ifndef RAMFILE_H
#define RAMFILE_H

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "migration/qemu-file.h"

typedef struct ram_disk {
	void *base;
	gsize len;
} ram_disk;

QEMUFile *qemu_fopen_ram(ram_disk **rd);
QEMUFile *qemu_fopen_ro_ram(ram_disk* rd);
void qemu_freopen_ro_ram(QEMUFile* f);

#endif
