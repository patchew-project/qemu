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

#ifndef CONFIG_USER_ONLY
#include CONFIG_DEVICES /* CONFIG_SEV */
#endif

#ifdef CONFIG_SEV
bool sev_enabled(void);
bool sev_es_enabled(void);
#else
#define sev_enabled() 0
#define sev_es_enabled() 0
#endif

uint32_t sev_get_cbit_position(void);
uint32_t sev_get_reduced_phys_bits(void);

int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);

#endif
