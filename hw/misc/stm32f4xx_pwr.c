/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) liang yan <yanl1229@rt-thread.org>
 * Copyright (c) Yihao Fan <fanyihao@rt-thread.org>
 * The reference used is the STMicroElectronics RM0090 Reference manual
 * https://www.st.com/en/microcontrollers-microprocessors/stm32f407-417/documentation.html
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/misc/stm32f4xx_pwr.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#ifndef STM32F4XX_PWR_DEBUG
#define STM32F4XX_PWR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM32F4XX_PWR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static uint64_t stm32f4xx_pwr_read(void *opaque, hwaddr offset, unsigned size)
{
    STM32F4XXPwrState *s = opaque;

    switch (offset) {
    case PWR_CR:
        return s->pwr_cr;
    case PWR_CSR:
        return s->pwr_csr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "STM32F4XX PWR: Bad read offset 0x%lx\n", offset);
        return 0;
    }
}

static void stm32f4xx_pwr_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    STM32F4XXPwrState *s = opaque;

    switch (offset) {
    case PWR_CR:
        s->pwr_cr = value;
        if (value & PWR_CR_ODEN)
            s->pwr_csr |= PWR_CSR_ODRDY;
        if (value & PWR_CR_ODSWEN)
            s->pwr_csr |= PWR_CSR_ODSWRDY;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "STM32F4XX PWR: Bad write offset 0x%lx\n", offset);
        break;
    }
}

static const MemoryRegionOps stm32f4xx_pwr_ops = {
    .read = stm32f4xx_pwr_read,
    .write = stm32f4xx_pwr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void stm32f4xx_pwr_init(Object *obj)
{
    STM32F4XXPwrState *s = STM32F4XX_PWR(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_pwr_ops, s, TYPE_STM32F4XX_PWR, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32f4xx_pwr_reset(DeviceState *dev)
{
    STM32F4XXPwrState *s = STM32F4XX_PWR(dev);

    s->pwr_cr  = 0x0000;
    s->pwr_csr = 0x0000;
}

static void stm32f4xx_pwr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f4xx_pwr_reset);
}

static const TypeInfo stm32f4xx_pwr_info = {
    .name          = TYPE_STM32F4XX_PWR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F4XXPwrState),
    .instance_init = stm32f4xx_pwr_init,
    .class_init    = stm32f4xx_pwr_class_init,
};

static void stm32f4xx_pwr_register_types(void)
{
    type_register_static(&stm32f4xx_pwr_info);
}

type_init(stm32f4xx_pwr_register_types)
