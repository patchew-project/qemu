/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2014-2015 Broadcom
 *
 * Author: Prem Mallappa <pmallapp@broadcom.com>
 *
 */
#ifndef HW_ARM_SMMU_H
#define HW_ARM_SMMU_H

#define TYPE_SMMU_DEV_BASE "smmu-base"
#define TYPE_SMMU_DEV      "smmuv3"

typedef enum {
    SMMU_IRQ_GERROR,
    SMMU_IRQ_PRIQ,
    SMMU_IRQ_EVTQ,
    SMMU_IRQ_CMD_SYNC,
} SMMUIrq;

#endif
