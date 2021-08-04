/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_H
#define QEMU_SEV_H

#include <qapi/qapi-types-migration.h>
#include "sysemu/kvm.h"

#define RAM_SAVE_ENCRYPTED_PAGE           0x1
#define RAM_SAVE_SHARED_REGIONS_LIST      0x2

bool sev_enabled(void);
int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);
int sev_encrypt_flash(uint8_t *ptr, uint64_t len, Error **errp);
int sev_save_setup(MigrationParameters *p);
int sev_save_outgoing_page(QEMUFile *f, uint8_t *ptr,
                           uint32_t size, uint64_t *bytes_sent);
int sev_load_incoming_page(QEMUFile *f, uint8_t *ptr);
int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp);

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size);
void sev_es_set_reset_vector(CPUState *cpu);
int sev_remove_shared_regions_list(unsigned long gfn_start,
                                   unsigned long gfn_end);
int sev_add_shared_regions_list(unsigned long gfn_start, unsigned long gfn_end);
int sev_save_outgoing_shared_regions_list(QEMUFile *f);
int sev_load_incoming_shared_regions_list(QEMUFile *f);
bool sev_is_gfn_in_unshared_region(unsigned long gfn);
void sev_del_migrate_blocker(void);

#endif
