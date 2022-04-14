/*
 * QEMU IOMMUFD
 *
 * Copyright (C) 2022 Intel Corporation.
 * Copyright Red Hat, Inc. 2022
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_IOMMUFD_IOMMUFD_H
#define HW_IOMMUFD_IOMMUFD_H
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"

int iommufd_get_ioas(int *fd, uint32_t *ioas_id);
void iommufd_put_ioas(int fd, uint32_t ioas_id);
int iommufd_unmap_dma(int iommufd, uint32_t ioas, hwaddr iova, ram_addr_t size);
int iommufd_map_dma(int iommufd, uint32_t ioas, hwaddr iova,
                    ram_addr_t size, void *vaddr, bool readonly);
int iommufd_copy_dma(int iommufd, uint32_t src_ioas, uint32_t dst_ioas,
                     hwaddr iova, ram_addr_t size, bool readonly);
bool iommufd_supported(void);
#endif /* HW_IOMMUFD_IOMMUFD_H */
