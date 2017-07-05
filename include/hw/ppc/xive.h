/*
 * QEMU PowerPC XIVE model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_XIVE_H
#define PPC_XIVE_H

#include "hw/ppc/xics.h"

typedef struct XIVE XIVE;
typedef struct XiveICSState XiveICSState;

#define TYPE_XIVE "xive"
#define XIVE(obj) OBJECT_CHECK(XIVE, (obj), TYPE_XIVE)

#define TYPE_ICS_XIVE "xive-source"
#define ICS_XIVE(obj) OBJECT_CHECK(XiveICSState, (obj), TYPE_ICS_XIVE)

struct XiveICSState {
    ICSState parent_obj;

    XIVE         *xive;
};

#endif /* PPC_XIVE_H */
