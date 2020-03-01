/*
 * STM32F4xx FLASHIF
 *
 * Copyright (c) 2020 Stephanos Ioannidis <root@stephanos.io>
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
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32f4xx_flashif.h"

static void stm32f4xx_flashif_reset(DeviceState *dev)
{
    STM32F4xxFlashIfState *s = STM32F4XX_FLASHIF(dev);

    /* Initialise states */
    s->cr_key_index = 0;
    s->optcr_key_index = 0;

    /* Initialise register values */
    s->flash_acr.reg = 0x00000000;
    s->flash_keyr.reg = 0x00000000;
    s->flash_optkeyr.reg = 0x00000000;
    s->flash_sr.reg = 0x00000000;
    s->flash_cr.reg = 0x80000000;
    s->flash_optcr.reg = 0x0FFFAAED;
}

static uint64_t stm32f4xx_flashif_read(void * opaque, hwaddr addr,
                                       unsigned int size)
{
    STM32F4xxFlashIfState *s = opaque;

    trace_stm32f4xx_flashif_read(addr);

    switch (addr) {
    case FLASH_ACR:
        return s->flash_acr.reg;
    case FLASH_SR:
        return s->flash_sr.reg;
    case FLASH_CR:
        return s->flash_cr.reg;
    case FLASH_OPTCR:
        return s->flash_optcr.reg;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }
}

static void flash_acr_write(STM32F4xxFlashIfState *s, FlashAcrType val)
{
    /* Set latency */
    s->flash_acr.latency = val.latency;
    /* Set prefetch status */
    s->flash_acr.prften = val.prften;
    /* Set instruction cache status */
    s->flash_acr.icen = val.icen;
    /* Set data cache status */
    s->flash_acr.dcen = val.dcen;
}

static void flash_cr_write(STM32F4xxFlashIfState *s, FlashCrType val)
{
    /* Lock FLASH_CR if lock bit is set */
    if (val.lock) {
        s->flash_cr.lock = 1;
        s->cr_key_index = 0;
        return;
    }
}

static void flash_optcr_write(STM32F4xxFlashIfState *s, FlashOptcrType val)
{
    /* Lock FLASH_OPTCR if lock bit is set */
    if (val.optlock) {
        s->flash_optcr.optlock = 1;
        s->optcr_key_index = 0;
        return;
    }
}

static void stm32f4xx_flashif_write(void *opaque, hwaddr addr,
                                    uint64_t val64, unsigned int size)
{
    STM32F4xxFlashIfState *s = opaque;
    uint32_t value = val64;

    trace_stm32f4xx_flashif_write(value, addr);

    switch (addr) {
    case FLASH_ACR:
        flash_acr_write(s, (FlashAcrType)value);
        return;
    case FLASH_KEYR:
        if (s->cr_key_index == 0 && value == 0x45670123) {
            s->cr_key_index = 1;
        } else if (s->cr_key_index == 1 && value == 0xCDEF89AB) {
            /* Valid key; unlock FLASH_CR */
            s->flash_cr.lock = 0;
            s->cr_key_index = 0;
        } else {
            /* Invalid key; permanently lock FLASH_CR until reset */
            s->flash_cr.lock = 1;
            s->cr_key_index = -1;
        }
        return;
    case FLASH_OPTKEYR:
        if (s->optcr_key_index == 0 && value == 0x08192A3B) {
            s->optcr_key_index = 1;
        } else if (s->optcr_key_index == 1 && value == 0x4C5D6E7F) {
            /* Valid key; unlock FLASH_OPTCR */
            s->flash_optcr.optlock = 0;
            s->optcr_key_index = 0;
        } else {
            /* Invalid key; lock FLASH_OPTCR until reset */
            s->flash_optcr.optlock = 1;
            s->optcr_key_index = -1;
        }
        return;
    case FLASH_SR:
        if (s->flash_sr.eop)        s->flash_sr.eop = 0;
        if (s->flash_sr.operr)      s->flash_sr.operr = 0;
        if (s->flash_sr.wrperr)     s->flash_sr.wrperr = 0;
        if (s->flash_sr.pgaerr)     s->flash_sr.pgaerr = 0;
        if (s->flash_sr.pgperr)     s->flash_sr.pgperr = 0;
        if (s->flash_sr.pgserr)     s->flash_sr.pgserr = 0;
        return;
    case FLASH_CR:
        flash_cr_write(s, (FlashCrType)value);
        return;
    case FLASH_OPTCR:
        flash_optcr_write(s, (FlashOptcrType)value);
        return;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f4xx_flashif_ops = {
    .read = stm32f4xx_flashif_read,
    .write = stm32f4xx_flashif_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32f4xx_flashif_init(Object *obj)
{
    STM32F4xxFlashIfState *s = STM32F4XX_FLASHIF(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_flashif_ops, s,
                          TYPE_STM32F4XX_FLASHIF, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_stm32f4xx_flashif = {
    .name = TYPE_STM32F4XX_FLASHIF,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(flash_acr.reg, STM32F4xxFlashIfState),
        VMSTATE_UINT32(flash_keyr.reg, STM32F4xxFlashIfState),
        VMSTATE_UINT32(flash_optkeyr.reg, STM32F4xxFlashIfState),
        VMSTATE_UINT32(flash_sr.reg, STM32F4xxFlashIfState),
        VMSTATE_UINT32(flash_cr.reg, STM32F4xxFlashIfState),
        VMSTATE_UINT32(flash_optcr.reg, STM32F4xxFlashIfState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f4xx_flashif_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f4xx_flashif_reset;
    dc->vmsd = &vmstate_stm32f4xx_flashif;
}

static const TypeInfo stm32f4xx_flashif_info = {
    .name          = TYPE_STM32F4XX_FLASHIF,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F4xxFlashIfState),
    .instance_init = stm32f4xx_flashif_init,
    .class_init    = stm32f4xx_flashif_class_init
};

static void stm32f4xx_flashif_register_types(void)
{
    type_register_static(&stm32f4xx_flashif_info);
}

type_init(stm32f4xx_flashif_register_types)
