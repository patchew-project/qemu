/*
 * Axiado Evaluation Kit Emulation
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

#include "qemu/osdep.h"
#include "hw/arm/axiado-boards.h"

static void axiado_scm3003_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Axiado SCM3003 EVK Board";
}

static const TypeInfo axiado_evk_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("axiado-scm3003"),
        .parent        = TYPE_AXIADO_MACHINE,
        .class_init    = axiado_scm3003_class_init,
    }
};

DEFINE_TYPES(axiado_evk_types)
