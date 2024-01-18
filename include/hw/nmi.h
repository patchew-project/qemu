/*
 *  NMI monitor handler class and helpers definitions.
 *
 *  Copyright IBM Corp., 2014
 *
 *  Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NMI_H
#define NMI_H

#include "qom/object.h"

#define TYPE_NMI "nmi"

typedef struct NMIClass NMIClass;
DECLARE_CLASS_CHECKERS(NMIClass, NMI,
                       TYPE_NMI)
#define NMI(obj) \
     INTERFACE_CHECK(NMIState, (obj), TYPE_NMI)

typedef struct NMIState NMIState;

struct NMIClass {
    InterfaceClass parent_class;

    void (*nmi_monitor_handler)(NMIState *n, int cpu_index, Error **errp);
};

void nmi_monitor_handle(int cpu_index, Error **errp);


#define TYPE_MCE "mce"

#define MCE_CLASS(klass) \
     OBJECT_CLASS_CHECK(MCEClass, (klass), TYPE_MCE)
#define MCE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MCEClass, (obj), TYPE_MCE)
#define MCE(obj) \
     INTERFACE_CHECK(MCEState, (obj), TYPE_MCE)

typedef struct MCEState MCEState;

typedef struct MCEClass {
    InterfaceClass parent_class;

    void (*mce_monitor_handler)(MCEState *n, const QDict *qdict, Error **errp);
} MCEClass;

void mce_monitor_handle(const QDict *qdict, Error **errp);

#endif /* NMI_H */
