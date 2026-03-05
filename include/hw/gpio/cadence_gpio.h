/*
 * Cadence GPIO registers definition.
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

#ifndef CADENCE_GPIO_H
#define CADENCE_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_CADENCE_GPIO "cadence_gpio"
OBJECT_DECLARE_SIMPLE_TYPE(CadenceGPIOState, CADENCE_GPIO)

#define CDNS_GPIO_REG_SIZE      0x400
#define CDNS_GPIO_NUM           32

#define CDNS_GPIO_BYPASS_MODE           0x00
#define CDNS_GPIO_DIRECTION_MODE        0x04
#define CDNS_GPIO_OUTPUT_EN             0x08
#define CDNS_GPIO_OUTPUT_VALUE          0x0c
#define CDNS_GPIO_INPUT_VALUE           0x10
#define CDNS_GPIO_IRQ_MASK              0x14
#define CDNS_GPIO_IRQ_EN                0x18
#define CDNS_GPIO_IRQ_DIS               0x1c
#define CDNS_GPIO_IRQ_STATUS            0x20
#define CDNS_GPIO_IRQ_TYPE              0x24
#define CDNS_GPIO_IRQ_VALUE             0x28
#define CDNS_GPIO_IRQ_ANY_EDGE          0x2c

struct CadenceGPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t bmr;
    uint32_t dmr;
    uint32_t oer;
    uint32_t ovr;
    uint32_t inpvr;
    uint32_t imr;
    uint32_t isr;
    uint32_t itr;
    uint32_t ivr;
    uint32_t ioar;
    qemu_irq irq;
    qemu_irq output[CDNS_GPIO_NUM];
};

#endif /* CADENCE_GPIO_H */
