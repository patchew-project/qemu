/*
 * QEMU IAA compression support
 *
 * Copyright (c) 2023 Intel Corporation
 *  Written by:
 *  Yuan Liu<yuan1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_IAA_COMPRESS_H
#define QEMU_MIGRATION_IAA_COMPRESS_H
#include "qemu-file.h"
#include "ram-compress.h"

int iaa_compress_init(bool is_decompression);
void iaa_compress_deinit(void);
#endif
