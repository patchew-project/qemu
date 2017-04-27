/*
 * ARMv7M stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/arm/armv7m.h"

void armv7m_nvic_acknowledge_irq(void *opaque)
{
    NVICState *s = (NVICState *)opaque;

    cpu_abort(CPU(s->cpu), "No NVIC avaialable to acknowledge IRQ\n");
}

void armv7m_nvic_set_pending(void *opaque, int irq)
{
    NVICState *s = (NVICState *)opaque;

    cpu_abort(CPU(s->cpu), "No NVIC avaialable to set IRQ pending\n");
}

int armv7m_nvic_complete_irq(void *opaque, int irq)
{
    NVICState *s = (NVICState *)opaque;

    cpu_abort(CPU(s->cpu), "No NVIC avaialable to complete IRQ\n");

    return 0;
}

bool armv7m_nvic_can_take_pending_exception(void *opaque)
{
    NVICState *s = (NVICState *)opaque;

    cpu_abort(CPU(s->cpu), "No NVIC avaialable to check pending exception\n");

    return false;
}
