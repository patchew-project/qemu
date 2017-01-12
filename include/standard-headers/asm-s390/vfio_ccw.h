/*
 * Interfaces for vfio-ccw
 *
 * Copyright IBM Corp. 2017
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 */

#ifndef _VFIO_CCW_H_
#define _VFIO_CCW_H_

#include "standard-headers/linux/types.h"

struct ccw_io_region {
#define ORB_AREA_SIZE 12
	uint8_t  orb_area[ORB_AREA_SIZE];
#define SCSW_AREA_SIZE 12
	uint8_t  scsw_area[SCSW_AREA_SIZE];
#define IRB_AREA_SIZE 96
	uint8_t  irb_area[IRB_AREA_SIZE];
	uint32_t ret_code;
} QEMU_PACKED;

#endif
