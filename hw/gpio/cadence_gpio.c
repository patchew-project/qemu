/*
 * Cadence GPIO emulation.
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
#include "hw/core/irq.h"
#include "hw/gpio/cadence_gpio.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

static void cdns_gpio_update_irq(CadenceGPIOState *s)
{
    qemu_set_irq(s->irq, s->isr ? 1 : 0);
}

static void cdns_gpio_update_isr_per_line(CadenceGPIOState *s, int line,
                                          uint32_t new)
{
    uint32_t old = extract32(s->inpvr, line, 1);
    uint32_t ivr = extract32(s->ivr, line, 1);

    /* Deassert in bypass mode or not input pin or masked */
    if (extract32(s->bmr, line, 1) || !extract32(s->dmr, line, 1) ||
        extract32(s->imr, line, 1)) {
        s->isr = deposit32(s->isr, line, 1, 0);
        return;
    }

    if (extract32(s->itr, line, 1)) {
        /* Level-triggered */
        if (ivr && new) {
            /* High level */
            s->isr = deposit32(s->isr, line, 1, 1);
        }
        if (!ivr && !new) {
            /* Low level */
            s->isr = deposit32(s->isr, line, 1, 1);
        }
    } else {
        /* Edge-triggered */
        if (extract32(s->ioar, line, 1) && (old != new)) {
            /* On any edge */
            s->isr = deposit32(s->isr, line, 1, 1);
        } else {
            if (ivr && !old && new) {
                /* Rising edge */
                s->isr = deposit32(s->isr, line, 1, 1);
            }
            if (!ivr && old && !new) {
                /* Falling edge */
                s->isr = deposit32(s->isr, line, 1, 1);
            }
        }
    }
}

static void cdns_gpio_update_isr(CadenceGPIOState *s)
{
    for (int i = 0; i < CDNS_GPIO_NUM; i++) {
        uint32_t level = extract32(s->inpvr, i, 1);
        cdns_gpio_update_isr_per_line(s, i, level);
    }
}

static void cdns_gpio_set(void *opaque, int line, int level)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);
    uint32_t new = level ? 1: 0;

    trace_cdns_gpio_set(DEVICE(s)->canonical_path, line, level);

    cdns_gpio_update_isr_per_line(s, line, new);

    /* Sync INPVR with new value */
    s->inpvr = deposit32(s->inpvr, line, 1, new);

    cdns_gpio_update_irq(s);
}

static inline void cdns_gpio_update_output_irq(CadenceGPIOState *s)
{
    for (int i = 0; i < CDNS_GPIO_NUM; i++) {
        /* Forward the output value to corresponding pin */
        if (!extract32(s->bmr, i, 1) && !extract32(s->dmr, i, 1) &&
            extract32(s->oer, i, 1) && s->output[i]) {
            qemu_set_irq(s->output[i], extract32(s->ovr, i, 1));
        }
    }
}

static uint64_t cdns_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);
    uint32_t reg_value = 0x0;

    switch (offset) {
    case CDNS_GPIO_BYPASS_MODE:
        reg_value = s->bmr;
        break;

    case CDNS_GPIO_DIRECTION_MODE:
        reg_value = s->dmr;
        break;

    case CDNS_GPIO_OUTPUT_EN:
        reg_value = s->oer;
        break;

    case CDNS_GPIO_OUTPUT_VALUE:
        reg_value = s->ovr;
        break;

    case CDNS_GPIO_INPUT_VALUE:
        reg_value = s->inpvr;
        break;

    case CDNS_GPIO_IRQ_MASK:
        reg_value = s->imr;
        break;

    case CDNS_GPIO_IRQ_STATUS:
        reg_value = s->isr;
        break;

    case CDNS_GPIO_IRQ_TYPE:
        reg_value = s->itr;
        break;

    case CDNS_GPIO_IRQ_VALUE:
        reg_value = s->ivr;
        break;

    case CDNS_GPIO_IRQ_ANY_EDGE:
        reg_value = s->ioar;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_CADENCE_GPIO, __func__, offset);
        break;
    }

    trace_cdns_gpio_read(DEVICE(s)->canonical_path, offset, reg_value);

    return reg_value;
}

static void cdns_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);

    trace_cdns_gpio_write(DEVICE(s)->canonical_path, offset, value);

    switch (offset) {
    case CDNS_GPIO_BYPASS_MODE:
        s->bmr = value;
        cdns_gpio_update_output_irq(s);
        cdns_gpio_update_isr(s);
        cdns_gpio_update_irq(s);
        break;

    case CDNS_GPIO_DIRECTION_MODE:
        s->dmr = value;
        cdns_gpio_update_output_irq(s);
        cdns_gpio_update_isr(s);
        cdns_gpio_update_irq(s);
        break;

    case CDNS_GPIO_OUTPUT_EN:
        s->oer = value;
        cdns_gpio_update_output_irq(s);
        break;

    case CDNS_GPIO_OUTPUT_VALUE:
        s->ovr = value;
        cdns_gpio_update_output_irq(s);
        break;

    case CDNS_GPIO_IRQ_EN:
        s->imr &= ~value;
        cdns_gpio_update_isr(s);
        cdns_gpio_update_irq(s);
        break;

    case CDNS_GPIO_IRQ_DIS:
        s->imr |= value;
        cdns_gpio_update_isr(s);
        cdns_gpio_update_irq(s);
        break;

    case CDNS_GPIO_IRQ_TYPE:
        s->itr = value;
        break;

    case CDNS_GPIO_IRQ_VALUE:
        s->ivr = value;
        break;

    case CDNS_GPIO_IRQ_ANY_EDGE:
        s->ioar = value;
        break;

    case CDNS_GPIO_INPUT_VALUE:
    case CDNS_GPIO_IRQ_MASK:
    case CDNS_GPIO_IRQ_STATUS:
        /* Read-Only */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_CADENCE_GPIO, __func__, offset);
        break;
    }
}

static const MemoryRegionOps cdns_gpio_ops = {
    .read = cdns_gpio_read,
    .write = cdns_gpio_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_cdns_gpio = {
    .name = TYPE_CADENCE_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(bmr, CadenceGPIOState),
        VMSTATE_UINT32(dmr, CadenceGPIOState),
        VMSTATE_UINT32(oer, CadenceGPIOState),
        VMSTATE_UINT32(ovr, CadenceGPIOState),
        VMSTATE_UINT32(inpvr, CadenceGPIOState),
        VMSTATE_UINT32(imr, CadenceGPIOState),
        VMSTATE_UINT32(isr, CadenceGPIOState),
        VMSTATE_UINT32(itr, CadenceGPIOState),
        VMSTATE_UINT32(ivr, CadenceGPIOState),
        VMSTATE_UINT32(ioar, CadenceGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void cdns_gpio_reset(DeviceState *dev)
{
    CadenceGPIOState *s = CADENCE_GPIO(dev);

    s->bmr      = 0;
    s->dmr      = 0;
    s->oer      = 0;
    s->ovr      = 0;
    s->inpvr    = 0;
    s->imr      = 0xffffffff;
    s->isr      = 0;
    s->itr      = 0;
    s->ivr      = 0;
    s->ioar     = 0;
}

static void cdns_gpio_init(Object *obj)
{
    CadenceGPIOState *s = CADENCE_GPIO(obj);

    memory_region_init_io(&s->iomem, obj, &cdns_gpio_ops, s,
                          TYPE_CADENCE_GPIO, CDNS_GPIO_REG_SIZE);

    qdev_init_gpio_in(DEVICE(s), cdns_gpio_set, CDNS_GPIO_NUM);
    qdev_init_gpio_out(DEVICE(s), s->output, CDNS_GPIO_NUM);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void cdns_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, cdns_gpio_reset);
    dc->vmsd = &vmstate_cdns_gpio;
    dc->desc = "Cadence GPIO controller";
}

static const TypeInfo cdns_gpio_info = {
    .name = TYPE_CADENCE_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceGPIOState),
    .instance_init = cdns_gpio_init,
    .class_init = cdns_gpio_class_init,
};

static void cdns_gpio_register_types(void)
{
    type_register_static(&cdns_gpio_info);
}

type_init(cdns_gpio_register_types)
