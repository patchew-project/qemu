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

    /**
     * nmi_handler: Callback to handle NMI notifications.
     *
     * @n: Class #NMIState state
     * @errp: pointer to error object
     *
     * On success, return %true.
     * On failure, store an error through @errp and return %false.
     */
    bool (*nmi_handler)(NMIState *n, Error **errp);
};

/**
 * nmi_trigger: Trigger a NMI.
 *
 * @errp: pointer to error object
 *
 * Iterate over all objects implementing the TYPE_NMI interface
 * and deliver NMI to them.
 *
 * On success, return %true.
 * On failure, store an error through @errp and return %false.
 */
bool nmi_trigger(Error **errp);

#endif /* NMI_H */
