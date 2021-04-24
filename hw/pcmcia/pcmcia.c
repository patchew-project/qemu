/*
 * PCMCIA emulation
 *
 * Copyright 2013 SUSE LINUX Products GmbH
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/pcmcia.h"

static void pcmcia_card_reset_handler(void *dev)
{
    device_legacy_reset(DEVICE(dev));
}

static void pcmcia_card_realize(DeviceState *dev, Error **errp)
{
    qemu_register_reset(pcmcia_card_reset_handler, dev);
}

static void pcmcia_card_unrealize(DeviceState *dev)
{
    qemu_unregister_reset(pcmcia_card_reset_handler, dev);
}

static void pcmcia_card_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pcmcia_card_realize;
    dc->unrealize = pcmcia_card_unrealize;
}

static const TypeInfo pcmcia_card_type_info = {
    .name = TYPE_PCMCIA_CARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCMCIACardState),
    .abstract = true,
    .class_size = sizeof(PCMCIACardClass),
    .class_init = pcmcia_card_class_init,
};

static void pcmcia_register_types(void)
{
    type_register_static(&pcmcia_card_type_info);
}

type_init(pcmcia_register_types)
