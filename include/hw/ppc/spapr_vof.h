/*
 * QEMU PowerPC sPAPR VOF Support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_SPAPR_VOF_H
#define HW_SPAPR_VOF_H

typedef struct BlockBackend BlockBackend;

bool spapr_vof_find_prep_partition(BlockBackend *blk,
                                   uint64_t *offset, uint64_t *size);

#endif /* HW_SPAPR_VOF_H */
