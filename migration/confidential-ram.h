/*
 * QEMU migration for confidential guest's RAM
 */

#ifndef QEMU_CONFIDENTIAL_RAM_H
#define QEMU_CONFIDENTIAL_RAM_H

#include "exec/cpu-common.h"
#include "qemu-file.h"

void cgs_mh_init(void);
void cgs_mh_cleanup(void);

int cgs_mh_save_encrypted_page(QEMUFile *f, ram_addr_t src_gpa, uint32_t size,
                               uint64_t *bytes_sent);

#endif
