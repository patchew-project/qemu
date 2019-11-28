/*
 * QEMU ATmega MCU
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/boards.h" /* FIXME memory_region_allocate_system_memory for sram */
#include "hw/misc/unimp.h"
#include "atmega.h"

enum AtmegaIrq {
    USART0_RXC_IRQ, USART0_DRE_IRQ, USART0_TXC_IRQ,
    USART1_RXC_IRQ, USART1_DRE_IRQ, USART1_TXC_IRQ,
    USART2_RXC_IRQ, USART2_DRE_IRQ, USART2_TXC_IRQ,
    USART3_RXC_IRQ, USART3_DRE_IRQ, USART3_TXC_IRQ,
    TIMER0_CAPT_IRQ, TIMER0_COMPA_IRQ, TIMER0_COMPB_IRQ,
        TIMER0_COMPC_IRQ, TIMER0_OVF_IRQ,
    TIMER1_CAPT_IRQ, TIMER1_COMPA_IRQ, TIMER1_COMPB_IRQ,
        TIMER1_COMPC_IRQ, TIMER1_OVF_IRQ,
    TIMER2_CAPT_IRQ, TIMER2_COMPA_IRQ, TIMER2_COMPB_IRQ,
        TIMER2_COMPC_IRQ, TIMER2_OVF_IRQ,
    TIMER3_CAPT_IRQ, TIMER3_COMPA_IRQ, TIMER3_COMPB_IRQ,
        TIMER3_COMPC_IRQ, TIMER3_OVF_IRQ,
    TIMER4_CAPT_IRQ, TIMER4_COMPA_IRQ, TIMER4_COMPB_IRQ,
        TIMER4_COMPC_IRQ, TIMER4_OVF_IRQ,
    TIMER5_CAPT_IRQ, TIMER5_COMPA_IRQ, TIMER5_COMPB_IRQ,
        TIMER5_COMPC_IRQ, TIMER5_OVF_IRQ,
};
#define IRQ_MAX             64

#define USART_RXC_IRQ(n)    (3 * n + USART0_RXC_IRQ)
#define USART_DRE_IRQ(n)    (3 * n + USART0_DRE_IRQ)
#define USART_TXC_IRQ(n)    (3 * n + USART0_TXC_IRQ)
#define TIMER_CAPT_IRQ(n)   (5 * n + TIMER0_CAPT_IRQ)
#define TIMER_COMPA_IRQ(n)  (5 * n + TIMER0_COMPA_IRQ)
#define TIMER_COMPB_IRQ(n)  (5 * n + TIMER0_COMPB_IRQ)
#define TIMER_COMPC_IRQ(n)  (5 * n + TIMER0_COMPC_IRQ)
#define TIMER_OVF_IRQ(n)    (5 * n + TIMER0_OVF_IRQ)

static const uint8_t irq168_328[IRQ_MAX] = {
    [TIMER2_COMPA_IRQ]      = 8,
    [TIMER2_COMPB_IRQ]      = 9,
    [TIMER2_OVF_IRQ]        = 10,
    [TIMER1_CAPT_IRQ]       = 11,
    [TIMER1_COMPA_IRQ]      = 12,
    [TIMER1_COMPB_IRQ]      = 13,
    [TIMER1_OVF_IRQ]        = 14,
    [TIMER0_COMPA_IRQ]      = 15,
    [TIMER0_COMPB_IRQ]      = 16,
    [TIMER0_OVF_IRQ]        = 17,
    [USART0_RXC_IRQ]        = 19,
    [USART0_DRE_IRQ]        = 20,
    [USART0_TXC_IRQ]        = 21,
}, irq1280_2560[IRQ_MAX] = {
    [TIMER2_COMPA_IRQ]      = 14,
    [TIMER2_COMPB_IRQ]      = 15,
    [TIMER2_OVF_IRQ]        = 16,
    [TIMER1_CAPT_IRQ]       = 17,
    [TIMER1_COMPA_IRQ]      = 18,
    [TIMER1_COMPB_IRQ]      = 19,
    [TIMER1_COMPC_IRQ]      = 20,
    [TIMER1_OVF_IRQ]        = 21,
    [TIMER0_COMPA_IRQ]      = 22,
    [TIMER0_COMPB_IRQ]      = 23,
    [TIMER0_OVF_IRQ]        = 24,
    [USART0_RXC_IRQ]        = 26,
    [USART0_DRE_IRQ]        = 27,
    [USART0_TXC_IRQ]        = 28,
    [TIMER3_CAPT_IRQ]       = 32,
    [TIMER3_COMPA_IRQ]      = 33,
    [TIMER3_COMPB_IRQ]      = 34,
    [TIMER3_COMPC_IRQ]      = 35,
    [TIMER3_OVF_IRQ]        = 36,
    [USART1_RXC_IRQ]        = 37,
    [USART1_DRE_IRQ]        = 38,
    [USART1_TXC_IRQ]        = 39,
    [USART2_RXC_IRQ]        = 52,
    [USART2_DRE_IRQ]        = 53,
    [USART2_TXC_IRQ]        = 54,
    [USART3_RXC_IRQ]        = 55,
    [USART3_DRE_IRQ]        = 56,
    [USART3_TXC_IRQ]        = 57,
};

enum AtmegaPeripheralAddress {
    USART0, USART1, USART2, USART3,
    TIMER0, TIMER1, TIMER2, TIMER3, TIMER4, TIMER5,
    DEV_MAX
};

#define USART_ADDR(n)       (n + USART0)
#define TIMER_ADDR(n)       (n + TIMER0)

typedef struct {
    uint16_t addr;
    uint16_t prr_addr;
    uint8_t prr_bit;
    /* timer specific */
    uint16_t intmask_addr;
    uint16_t intflag_addr;
    bool is_timer16;
} peripheral_cfg;

static const peripheral_cfg dev168_328[DEV_MAX] = {
    [TIMER0]        = {  0x24, 0x64, 5, 0x6e, 0x35, false },
    [TIMER1]        = {  0x80, 0x64, 3, 0x6f, 0x36, true },
    [TIMER2]        = {  0xb0, 0x64, 6, 0x70, 0x37, false },
    [USART0]        = {  0xc0, 0x64, 1 },
}, dev1280_2560[DEV_MAX] = {
    [TIMER0]        = {  0x24, 0x64, 5, 0x6e, 0x35, false },
    [TIMER1]        = {  0x80, 0x64, 3, 0x6f, 0x36, true },
    [TIMER3]        = {  0x90, 0x65, 3, 0x71, 0x38, true },
    [TIMER4]        = {  0xa0, 0x65, 4, 0x72, 0x39, true },
    [TIMER2]        = {  0xb0, 0x64, 6, 0x70, 0x37, false },
    [USART0]        = {  0xc0, 0x64, 1 },
    [USART1]        = {  0xc8, 0x65, 0 },
    [USART2]        = {  0xd0, 0x65, 1 },
    [TIMER5]        = { 0x120, 0x65, 5, 0x73, 0x3a, true },
    [USART3]        = { 0x130, 0x65, 2 },
};

struct AtmegaInfo {
    const char *uc_name;
    const char *cpu_type;
    size_t flash_size;
    size_t eeprom_size;
    size_t sram_size;
    size_t io_size;
    size_t uart_count;
    size_t timer_count;
    size_t gpio_count;
    size_t adc_count;
    const uint8_t *irq;
    const peripheral_cfg *dev;
};

static const AtmegaInfo atmega_mcu[] = {
    {
        .uc_name = TYPE_ATMEGA168,
        .cpu_type = AVR_CPU_TYPE_NAME("avr5"),
        .flash_size = 16 * KiB,
        .eeprom_size = 512,
        .sram_size = 1 * KiB,
        .io_size = 256,
        .uart_count = 1,
        .gpio_count = 23,
        .adc_count = 6,
        .irq = irq168_328,
        .dev = dev168_328,
    },
    {
        .uc_name = TYPE_ATMEGA328,
        .cpu_type = AVR_CPU_TYPE_NAME("avr5"),
        .flash_size = 32 * KiB,
        .eeprom_size = 1 * KiB,
        .sram_size = 2 * KiB,
        .io_size = 256,
        .uart_count = 1,
        .timer_count = 3,
        .gpio_count = 23,
        .adc_count = 6,
        .irq = irq168_328,
        .dev = dev168_328,
    },
    {
        .uc_name = TYPE_ATMEGA1280,
        .cpu_type = AVR_CPU_TYPE_NAME("avr6"),
        .flash_size = 128 * KiB,
        .eeprom_size = 4 * KiB,
        .sram_size = 8 * KiB,
        .io_size = 512,
        .uart_count = 4,
        .timer_count = 6,
        .gpio_count = 86,
        .adc_count = 16,
        .irq = irq1280_2560,
        .dev = dev1280_2560,
    },
    {
        .uc_name = TYPE_ATMEGA2560,
        .cpu_type = AVR_CPU_TYPE_NAME("avr6"),
        .flash_size = 256 * KiB,
        .eeprom_size = 4 * KiB,
        .sram_size = 8 * KiB,
        .io_size = 512,
        .uart_count = 4,
        .timer_count = 6,
        .gpio_count = 54,
        .adc_count = 16,
        .irq = irq1280_2560,
        .dev = dev1280_2560,
    },
};

static void connect_nonnull_irq(SysBusDevice *sbd, DeviceState *dev,
                                int n, int irq)
{
    if (irq) {
        sysbus_connect_irq(sbd, n, qdev_get_gpio_in(dev, irq));
    }
}

static void connect_pr_irq(AtmegaState *s, const AtmegaInfo *info,
                           DeviceState *dev, int index)
{
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pwr[info->dev[index].prr_addr & 1]),
                       info->dev[index].prr_bit,
                       qdev_get_gpio_in(dev, 0));
}

static void atmega_realize(DeviceState *dev, Error **errp)
{
    AtmegaState *s = ATMEGA(dev);
    AtmegaClass *bc = ATMEGA_GET_CLASS(dev);
    const AtmegaInfo *info = bc->info;
    DeviceState *cpudev;
    SysBusDevice *sbd;
    Error *err = NULL;
    char *devname;
    size_t i;

    if (!s->xtal_freq_hz) {
        error_setg(errp, "\"xtal-frequency-hz\" property must be provided.");
        return;
    }

    /* CPU */
    object_initialize_child(OBJECT(dev), "cpu", &s->cpu, sizeof(s->cpu),
                            info->cpu_type, &err, NULL);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &error_abort);
    cpudev = DEVICE(&s->cpu);

    /* SRAM */
    memory_region_allocate_system_memory(&s->sram, OBJECT(dev),
                                         "sram", info->sram_size);
    memory_region_add_subregion(get_system_memory(),
                                OFFSET_DATA + 0x200, &s->sram);

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev),
                           "flash", info->flash_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), OFFSET_CODE, &s->flash);

    /* I/O */
    s->io = qdev_create(NULL, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(s->io, "name", "I/O");
    qdev_prop_set_uint64(s->io, "size", info->io_size);
    qdev_init_nofail(s->io);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->io), 0, OFFSET_DATA, -1234);

    /* Power */
    for (i = 0; i < ARRAY_SIZE(s->pwr); i++) {
        devname = g_strdup_printf("pwr%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->pwr[i], sizeof(s->pwr[i]),
                                TYPE_AVR_MASK, &error_abort, NULL);
        object_property_set_bool(OBJECT(&s->pwr[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwr[i]), 0, OFFSET_DATA + 0x64 + i);
        g_free(devname);
    }

    /* USART */
    for (i = 0; i < info->uart_count; i++) {
        devname = g_strdup_printf("usart%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->usart[i], sizeof(s->usart[i]),
                                TYPE_AVR_USART, &error_abort, NULL);
        qdev_prop_set_chr(DEVICE(&s->usart[i]), "chardev", serial_hd(i));
        object_property_set_bool(OBJECT(&s->usart[i]), true, "realized",
                                 &error_abort);
        sbd = SYS_BUS_DEVICE(&s->usart[i]);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + info->dev[USART_ADDR(i)].addr);
        connect_nonnull_irq(sbd, cpudev, 0, info->irq[USART_RXC_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 1, info->irq[USART_DRE_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 2, info->irq[USART_TXC_IRQ(i)]);
        connect_pr_irq(s, info, DEVICE(&s->usart[i]), 0);
        g_free(devname);
    }

    /* Timer */
    for (i = 0; i < info->timer_count; i++) {
        int idx = TIMER_ADDR(i);
        if (!info->dev[idx].is_timer16) {
            create_unimplemented_device("avr-timer8",
                                        OFFSET_DATA + info->dev[idx].addr, 7);
            create_unimplemented_device("avr-timer8-intmask",
                                        OFFSET_DATA
                                        + info->dev[idx].intmask_addr, 1);
            create_unimplemented_device("avr-timer8-intflag",
                                        OFFSET_DATA
                                        + info->dev[idx].intflag_addr, 1);
            continue;
        }
        devname = g_strdup_printf("timer%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->timer[i], sizeof(s->timer[i]),
                                TYPE_AVR_TIMER16, &error_abort, NULL);
        object_property_set_uint(OBJECT(&s->timer[i]), s->xtal_freq_hz,
                                 "cpu-frequency-hz", &error_abort);
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized",
                                 &error_abort);
        sbd = SYS_BUS_DEVICE(&s->timer[i]);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + info->dev[idx].addr);
        sysbus_mmio_map(sbd, 1, OFFSET_DATA + info->dev[idx].intmask_addr);
        sysbus_mmio_map(sbd, 2, OFFSET_DATA + info->dev[idx].intflag_addr);
        connect_nonnull_irq(sbd, cpudev, 0, info->irq[TIMER_CAPT_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 1, info->irq[TIMER_COMPA_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 2, info->irq[TIMER_COMPB_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 3, info->irq[TIMER_COMPC_IRQ(i)]);
        connect_nonnull_irq(sbd, cpudev, 4, info->irq[TIMER_OVF_IRQ(i)]);
        connect_pr_irq(s, info, DEVICE(&s->timer[i]), 0);
        g_free(devname);
    }
}

static Property atmega_props[] = {
    DEFINE_PROP_UINT64("xtal-frequency-hz", AtmegaState,
                       xtal_freq_hz, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void atmega_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AtmegaClass *bc = ATMEGA_CLASS(oc);

    bc->info = data;
    dc->realize = atmega_realize;
    dc->props = atmega_props;
    /* Reason: Mapped at fixed location on the system bus */
    dc->user_creatable = false;
}

static const TypeInfo atmega_type_info = {
    .name = TYPE_ATMEGA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AtmegaState),
    .class_size = sizeof(AtmegaClass),
    .abstract = true,
};

static void atmega_register_types(void)
{
    size_t i;

    type_register_static(&atmega_type_info);
    for (i = 0; i < ARRAY_SIZE(atmega_mcu); i++) {
        assert(atmega_mcu[i].io_size <= 0x200);
        assert(atmega_mcu[i].uart_count <= USART_MAX);
        assert(atmega_mcu[i].timer_count <= TIMER_MAX);
        TypeInfo ti = {
            .name = atmega_mcu[i].uc_name,
            .parent = TYPE_ATMEGA,
            .class_init = atmega_class_init,
            .class_data = (void *) &atmega_mcu[i],
        };
        type_register(&ti);
    }
}

type_init(atmega_register_types)
