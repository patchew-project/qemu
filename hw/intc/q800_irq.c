/*
 * QEMU Motorla 680x0 Macintosh hardware System Emulator
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/intc/q800_irq.h"


static void q800_set_irq(void *opaque, int irq, int level)
{
    Q800IRQControllerState *s = opaque;
    int i;


    if (level) {
        s->ipr |= 1 << irq;
    } else {
        s->ipr &= ~(1 << irq);
    }

    for (i = 7; i >= 0; i--) {
        if ((s->ipr >> i) & 1) {
            m68k_set_irq_level(s->cpu, i + 1, i + 25);
            return;
        }
    }
    m68k_set_irq_level(s->cpu, 0, 0);
}

static void q800_irq_init(Object *obj)
{
    Q800IRQControllerState *s = Q800_IRQC(obj);

    qdev_init_gpio_in(DEVICE(obj), q800_set_irq, 8);

    object_property_add_link(obj, "cpu", TYPE_M68K_CPU,
                             (Object **) &s->cpu,
                             qdev_prop_allow_set_link_before_realize,
                             0, NULL);
}

static const TypeInfo q800_irq_type_info = {
    .name = TYPE_Q800_IRQC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Q800IRQControllerState),
    .instance_init = q800_irq_init,
};

static void q800_irq_register_types(void)
{
    type_register_static(&q800_irq_type_info);
}

type_init(q800_irq_register_types);
