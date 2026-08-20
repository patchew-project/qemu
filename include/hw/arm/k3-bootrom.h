/*
 * TI K3 boot-ROM (RBL) emulation - X.509 combined image loading
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ARM_K3_BOOTROM_H
#define HW_ARM_K3_BOOTROM_H

#include "qapi/error.h"

#define K3_BOOTROM_MAX_COMPS 8

/* comp_type values from TI combined-image certificate. */
#define K3_COMP_TYPE_SBL        1
#define K3_COMP_TYPE_SYSFW      2
#define K3_COMP_TYPE_SYSFW_DATA 18

typedef struct K3BootComponent {
    uint32_t comp_type;
    uint32_t boot_core;
    uint32_t comp_opts;
    uint64_t dest_addr;
    uint32_t comp_size;
    size_t payload_offset;
} K3BootComponent;

typedef struct K3BootImage {
    uint32_t num_comps;
    uint64_t ext_img_size;
    size_t cert_len;
    K3BootComponent comps[K3_BOOTROM_MAX_COMPS];
} K3BootImage;

bool k3_bootrom_parse(const uint8_t *buf, size_t len, K3BootImage *out,
                      Error **errp);

typedef struct TIAM64xState TIAM64xState;
void k3_bootrom_load(TIAM64xState *soc, const char *filename, Error **errp);

#endif
