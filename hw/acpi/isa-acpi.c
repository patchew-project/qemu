#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/acpi/acpi.h"
#include "sysemu/runstate.h"

typedef struct ISAACPI {
    ISADevice base;

    uint32_t io_base;
    uint16_t sci_irq;
    uint32_t gpe_base;
    uint32_t gpe_len;

    qemu_irq irq;
    MemoryRegion io;
    ACPIREGS acpi;
    Notifier powerdown_req;
} ISAACPI;

#define TYPE_ISA_ACPI "isa-acpi"
#define ISA_ACPI(obj) \
    OBJECT_CHECK(ISAACPI, (obj), TYPE_ISA_ACPI)

static void isa_acpi_timer(ACPIREGS *acpi)
{
    ISAACPI *s = container_of(acpi, ISAACPI, acpi);
    acpi_update_sci(&s->acpi, s->irq);
}

static void isa_acpi_init(Object *obj)
{
    ISAACPI *s = ISA_ACPI(obj);

    s->io_base = 0x600;
    s->sci_irq = 9;
    s->gpe_base = 0x680;
    s->gpe_len = 4;
}

static void isa_acpi_powerdown_req(Notifier *n, void *opaque)
{
    ISAACPI *s = container_of(n, ISAACPI, powerdown_req);

    acpi_pm1_evt_power_down(&s->acpi);
}

static void isa_acpi_add_propeties(ISAACPI *s)
{
    static const uint8_t zero;

    object_property_add_uint8_ptr(OBJECT(s), ACPI_PM_PROP_ACPI_ENABLE_CMD,
                                  &zero, NULL);
    object_property_add_uint8_ptr(OBJECT(s), ACPI_PM_PROP_ACPI_DISABLE_CMD,
                                  &zero, NULL);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_GPE0_BLK,
                                   &s->gpe_base, NULL);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_GPE0_BLK_LEN,
                                   &s->gpe_len, NULL);
    object_property_add_uint16_ptr(OBJECT(s), ACPI_PM_PROP_SCI_INT,
                                   &s->sci_irq, NULL);
    object_property_add_uint32_ptr(OBJECT(s), ACPI_PM_PROP_PM_IO_BASE,
                                   &s->io_base, NULL);
}

static void isa_acpi_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isa = ISA_DEVICE(dev);
    ISAACPI *s = ISA_ACPI(dev);

    memory_region_init(&s->io, OBJECT(s), "isa-acpi", 64);
    memory_region_set_enabled(&s->io, true);
    isa_register_ioport(isa, &s->io, s->io_base);
    isa_init_irq(isa, &s->irq, s->sci_irq);

    acpi_pm_tmr_init(&s->acpi, isa_acpi_timer, &s->io);
    acpi_pm1_evt_init(&s->acpi, isa_acpi_timer, &s->io);
    acpi_pm1_cnt_init(&s->acpi, &s->io, true, true, 0);
    acpi_gpe_init(&s->acpi, 4);

    s->powerdown_req.notify = isa_acpi_powerdown_req;
    qemu_register_powerdown_notifier(&s->powerdown_req);

    isa_acpi_add_propeties(s);
}

static void isa_acpi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(klass);

    dc->realize = isa_acpi_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    adevc->madt_cpu = pc_madt_cpu_entry;
}

static const TypeInfo isa_acpi_info = {
    .name          = TYPE_ISA_ACPI,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAACPI),
    .instance_init = isa_acpi_init,
    .class_init    = isa_acpi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&isa_acpi_info);
}

type_init(register_types)
