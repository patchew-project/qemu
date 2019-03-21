/*
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/mem/pc-dimm.h"

static hwaddr ged_io_base;
static GedEvent *ged_events;
static uint32_t ged_events_size;

static Aml *ged_event_aml(const GedEvent *event)
{

    if (!event) {
        return NULL;
    }

    switch (event->event) {
    case GED_MEMORY_HOTPLUG:
        /* We run a complete memory SCAN when getting a memory hotplug event */
        return aml_call0(MEMORY_DEVICES_CONTAINER "." MEMORY_SLOT_SCAN_METHOD);
    default:
        break;
    }

    return NULL;
}

/*
 * The ACPI Generic Event Device (GED) is a hardware-reduced specific
 * device[ACPI v6.1 Section 5.6.9] that handles all platform events,
 * including the hotplug ones. Platforms need to specify their own
 * GedEvent array to describe what kind of events they want to support
 * through GED. This routine uses a single interrupt for the GED device,
 * relying on IO memory region to communicate the type of device
 * affected by the interrupt. This way, we can support up to 32 events
 * with a unique interrupt.
 */
void build_ged_aml(Aml *table, const char *name, uint32_t ged_irq,
                   AmlRegionSpace rs)
{
    Aml *crs = aml_resource_template();
    Aml *evt, *field;
    Aml *dev = aml_device("%s", name);
    Aml *irq_sel = aml_local(0);
    Aml *isel = aml_name(AML_GED_IRQ_SEL);
    uint32_t i;

    if (!ged_io_base || !ged_events || !ged_events_size) {
        return;
    }

    /* _CRS interrupt */
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_EDGE, AML_ACTIVE_HIGH,
                                  AML_EXCLUSIVE, &ged_irq, 1));
    /*
     * For each GED event we:
     * - Add an interrupt to the CRS section.
     * - Add a conditional block for each event, inside a while loop.
     *   This is semantically equivalent to a switch/case implementation.
     */
    evt = aml_method("_EVT", 1, AML_SERIALIZED);
    {
        Aml *ged_aml;
        Aml *if_ctx;

        /* Local0 = ISEL */
        aml_append(evt, aml_store(isel, irq_sel));

        /*
         * Here we want to call a method for each supported GED event type.
         * The resulting ASL code looks like:
         *
         * Local0 = ISEL
         * If ((Local0 & irq0) == irq0)
         * {
         *     MethodEvent0()
         * }
         *
         * If ((Local0 & irq1) == irq1)
         * {
         *     MethodEvent1()
         * }
         *
         * If ((Local0 & irq2) == irq2)
         * {
         *     MethodEvent2()
         * }
         */

        for (i = 0; i < ged_events_size; i++) {
            ged_aml = ged_event_aml(&ged_events[i]);
            if (!ged_aml) {
                continue;
            }

            /* If ((Local1 == irq))*/
            if_ctx = aml_if(aml_equal(aml_and(irq_sel, aml_int(ged_events[i].selector), NULL), aml_int(ged_events[i].selector)));
            {
                /* AML for this specific type of event */
                aml_append(if_ctx, ged_aml);
            }

            /*
             * We append the first "if" to the "while" context.
             * Other "ifs" will be "elseifs".
             */
            aml_append(evt, if_ctx);
        }
    }

    aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0013")));
    aml_append(dev, aml_name_decl("_UID", aml_string(GED_DEVICE)));
    aml_append(dev, aml_name_decl("_CRS", crs));

    /* Append IO region */
    aml_append(dev, aml_operation_region(AML_GED_IRQ_REG, rs,
               aml_int(ged_io_base + ACPI_GED_IRQ_SEL_OFFSET),
               ACPI_GED_IRQ_SEL_LEN));
    field = aml_field(AML_GED_IRQ_REG, AML_DWORD_ACC, AML_NOLOCK,
                      AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field(AML_GED_IRQ_SEL,
                                      ACPI_GED_IRQ_SEL_LEN * 8));
    aml_append(dev, field);

    /* Append _EVT method */
    aml_append(dev, evt);

    aml_append(table, dev);
}

/* Memory read by the GED _EVT AML dynamic method */
static uint64_t ged_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    GEDState *ged_st = opaque;

    switch (addr) {
    case ACPI_GED_IRQ_SEL_OFFSET:
        /* Read the selector value and reset it */
        qemu_mutex_lock(&ged_st->lock);
        val = ged_st->sel;
        ged_st->sel = 0;
        qemu_mutex_unlock(&ged_st->lock);
        break;
    default:
        break;
    }

    return val;
}

/* Nothing is expected to be written to the GED memory region */
static void ged_write(void *opaque, hwaddr addr, uint64_t data,
                      unsigned int size)
{
}

static const MemoryRegionOps ged_ops = {
    .read = ged_read,
    .write = ged_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void acpi_ged_event(GEDState *ged_st, uint32_t ged_irq_sel)
{
    /*
     * Set the GED IRQ selector to the expected device type value. This
     * way, the ACPI method will be able to trigger the right code based
     * on a unique IRQ.
     */
    qemu_mutex_lock(&ged_st->lock);
    ged_st->sel = ged_irq_sel;
    qemu_mutex_unlock(&ged_st->lock);

    /* Trigger the event by sending an interrupt to the guest. */
    qemu_irq_pulse(ged_st->gsi[ged_st->irq]);
}

static void virt_device_plug_cb(HotplugHandler *hotplug_dev,
                                DeviceState *dev, Error **errp)
{
    VirtAcpiState *s = VIRT_ACPI(hotplug_dev);

    if (s->memhp_state.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
            acpi_memory_plug_cb(hotplug_dev, &s->memhp_state,
                                dev, errp);
    } else {
        error_setg(errp, "virt: device plug request for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void virt_send_ged(AcpiDeviceIf *adev, AcpiEventStatusBits ev)
{
    VirtAcpiState *s = VIRT_ACPI(adev);
    uint32_t sel;

    if (ev & ACPI_MEMORY_HOTPLUG_STATUS) {
        sel = ACPI_GED_IRQ_SEL_MEM;
    } else {
        /* Unknown event. Return without generating interrupt. */
        return;
    }

    /*
     * We inject the hotplug interrupt. The IRQ selector will make
     * the difference from the ACPI table.
     */
    acpi_ged_event(&s->ged_state, sel);
}

static void virt_device_realize(DeviceState *dev, Error **errp)
{
    VirtAcpiState *s = VIRT_ACPI(dev);

    if (s->memhp_state.is_enabled) {
        acpi_memory_hotplug_init(get_system_memory(), OBJECT(dev),
                                 &s->memhp_state,
                                 s->memhp_base);
    }
}

static Property virt_acpi_properties[] = {
    DEFINE_PROP_UINT64("memhp_base", VirtAcpiState, memhp_base, 0),
    DEFINE_PROP_BOOL("memory-hotplug-support", VirtAcpiState,
                     memhp_state.is_enabled, true),
    DEFINE_PROP_PTR("gsi", VirtAcpiState, gsi),
    DEFINE_PROP_UINT64("ged_base", VirtAcpiState, ged_base, 0),
    DEFINE_PROP_UINT32("ged_irq", VirtAcpiState, ged_irq, 0),
    DEFINE_PROP_PTR("ged_events", VirtAcpiState, ged_events),
    DEFINE_PROP_UINT32("ged_events_size", VirtAcpiState, ged_events_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virt_acpi_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(class);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(class);

    dc->desc = "ACPI";
    dc->props = virt_acpi_properties;
    dc->realize = virt_device_realize;

    /* Reason: pointer properties "gsi" and "gde_events" */
    dc->user_creatable = false;

    hc->plug = virt_device_plug_cb;

    adevc->send_event = virt_send_ged;
}

static const TypeInfo virt_acpi_info = {
    .name          = TYPE_VIRT_ACPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtAcpiState),
    .class_init    = virt_acpi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void virt_acpi_register_types(void)
{
    type_register_static(&virt_acpi_info);
}

type_init(virt_acpi_register_types)
