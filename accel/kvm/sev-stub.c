/*
 * QEMU SEV stub
 *
 * Copyright Advanced Micro Devices 2018
 *
 * Authors:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sev.h"

int sev_encrypt_data(void *handle, uint8_t *ptr, uint64_t len)
{
    abort();
}

void *sev_guest_init(const char *id)
{
    return NULL;
}

int sev_save_setup(void *handle, const char *pdh, const char *plat_cert,
                   const char *amd_cert)
{
    return 1;
}

int sev_save_outgoing_page(void *handle, QEMUFile *f, uint8_t *ptr,
                           uint32_t size, uint64_t *bytes_sent)
{
    return 1;
}
