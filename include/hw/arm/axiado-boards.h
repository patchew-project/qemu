/*
 * Axiado Boards
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AXIADO_BOARD_H
#define AXIADO_BOARD_H

#include "hw/core/boards.h"
#include "hw/arm/axiado-soc.h"

#define TYPE_AXIADO_MACHINE       MACHINE_TYPE_NAME("axiado")
OBJECT_DECLARE_TYPE(AxiadoMachineState, AxiadoMachineClass, AXIADO_MACHINE)

typedef struct AxiadoMachineState {
    MachineState parent;

    AxiadoSoCState *soc;
} AxiadoMachineState;

typedef struct AxiadoMachineClass {
    MachineClass parent;

    const char *soc_type;
} AxiadoMachineClass;

#endif
