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

typedef int (*send_iaa_data) (RAMBlock *block, ram_addr_t offset, uint8_t *data,
                              uint32_t data_len, CompressResult result);

int iaa_compress_init(bool is_decompression);
void iaa_compress_deinit(void);
int compress_page_with_iaa(RAMBlock *block, ram_addr_t offset,
                           send_iaa_data send_page);
int decompress_data_with_iaa(QEMUFile *f, void *host, int len);
int flush_iaa_jobs(bool flush_all_jobs, send_iaa_data send_page);
#endif
