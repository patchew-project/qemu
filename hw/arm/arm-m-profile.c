/*
 * ARM M Profile System emulation.
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/arm/arm-m-profile.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "target/arm/idau.h"

static void arm_m_profile_instance_init(Object *obj)
{
    ARMMProfileState *s = ARM_M_PROFILE(obj);

    /* Can't init the cpu here, we don't yet know which model to use */

    memory_region_init(&s->container, obj, "arm-m-profile-container",
                       UINT64_MAX);

    object_initialize(&s->nvic, sizeof(s->nvic), TYPE_NVIC);
    qdev_set_parent_bus(DEVICE(&s->nvic), sysbus_get_default());
    object_property_add_alias(obj, "num-irq",
                              OBJECT(&s->nvic), "num-irq", &error_abort);
}

static void arm_m_profile_realize(DeviceState *dev, Error **errp)
{
    ARMMProfileState *s = ARM_M_PROFILE(dev);
    ARMMProfileClass *mc = ARM_M_PROFILE_GET_CLASS(dev);
    SysBusDevice *sbd;
    Error *err = NULL;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    s->cpu = ARM_CPU(object_new(s->cpu_type));

    object_property_set_link(OBJECT(s->cpu), OBJECT(&s->container), "memory",
                             &error_abort);

    /* Tell the CPU where the NVIC is; it will fail realize if it doesn't
     * have one.
     */
    s->cpu->env.nvic = &s->nvic;

    if (mc->cpu_init) {
        mc->cpu_init(s, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }

    object_property_set_bool(OBJECT(s->cpu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    /* Note that we must realize the NVIC after the CPU */
    object_property_set_bool(OBJECT(&s->nvic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    /* Alias the NVIC's input and output GPIOs as our own so the board
     * code can wire them up. (We do this in realize because the
     * NVIC doesn't create the input GPIO array until realize.)
     */
    qdev_pass_gpios(DEVICE(&s->nvic), dev, NULL);
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "SYSRESETREQ");

    /* Wire the NVIC up to the CPU */
    sbd = SYS_BUS_DEVICE(&s->nvic);
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));

    memory_region_add_subregion(&s->container, 0xe000e000,
                                sysbus_mmio_get_region(sbd, 0));
}

static Property arm_m_profile_properties[] = {
    DEFINE_PROP_STRING("cpu-type", ARMMProfileState, cpu_type),
    DEFINE_PROP_LINK("memory", ARMMProfileState, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_m_profile_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = arm_m_profile_realize;
    dc->props = arm_m_profile_properties;
}

static const TypeInfo arm_m_profile_info = {
    .name = TYPE_ARM_M_PROFILE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMMProfileState),
    .instance_init = arm_m_profile_instance_init,
    .abstract = true,
    .class_size = sizeof(ARMMProfileClass),
    .class_init = arm_m_profile_class_init,
};

static void arm_m_profile_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

void arm_m_profile_load_kernel(ARMCPU *cpu, const char *kernel_filename, int mem_size)
{
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int big_endian;
    AddressSpace *as;
    int asidx;
    CPUState *cs = CPU(cpu);

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (!kernel_filename && !qtest_enabled()) {
        error_report("Guest image must be specified (using -kernel)");
        exit(1);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
        asidx = ARMASIdx_S;
    } else {
        asidx = ARMASIdx_NS;
    }
    as = cpu_get_address_space(cs, asidx);

    if (kernel_filename) {
        image_size = load_elf_as(kernel_filename, NULL, NULL, &entry, &lowaddr,
                                 NULL, big_endian, EM_ARM, 1, 0, as);
        if (image_size < 0) {
            image_size = load_image_targphys_as(kernel_filename, 0,
                                                mem_size, as);
            lowaddr = 0;
        }
        if (image_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    /* CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. Unlike
     * A-profile CPUs, we don't need to do anything special in the
     * handler to arrange that it starts correctly.
     * This is arguably the wrong place to do this, but it matches the
     * way A-profile does it. Note that this means that every M profile
     * board must call this function!
     */
    qemu_register_reset(arm_m_profile_reset, cpu);
}

static void arm_m_profile_register_types(void)
{
    type_register_static(&arm_m_profile_info);
}

type_init(arm_m_profile_register_types)
