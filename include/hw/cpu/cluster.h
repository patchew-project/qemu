/*
 * QEMU CPU cluster
 *
 * Copyright (c) 2018 GreenSocs SAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_HW_CPU_CLUSTER_H
#define QEMU_HW_CPU_CLUSTER_H

#include "qemu/osdep.h"
#include "hw/qdev.h"

#define TYPE_CPU_CLUSTER "cpu-cluster"
#define CPU_CLUSTER(obj) \
    OBJECT_CHECK(CPUClusterState, (obj), TYPE_CPU_CLUSTER)

typedef struct CPUClusterState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    uint32_t cluster_id;
} CPUClusterState;

#endif
