/*
 * STM32F4xx RCC
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
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32f4xx_rcc.h"
#include "hw/timer/armv7m_systick.h"

#define HSI_FREQ    (16000000)

static void rcc_update_clock(STM32F4xxRccState *s)
{
    uint32_t pll_src, pll_prediv, pll_mult, pll_postdiv, pll_freq;
    uint32_t ahb_div, ahb_freq;
    uint32_t sysclk_freq;
    uint32_t systick_freq;

    /* Resolve PLL clock source */
    pll_src = s->rcc_pllcfgr.pllsrc ? s->hse_frequency : HSI_FREQ;

    /* Resolve PLL input division factor */
    switch (s->rcc_pllcfgr.pllm) {
    case 0b000010 ... 0b111111:
        pll_prediv = s->rcc_pllcfgr.pllm;
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Invalid PLLM\n", __func__);
        return;
    }

    /* Resolve PLL VCO multiplication factor */
    switch (s->rcc_pllcfgr.plln) {
    case 0b000110010 ... 0b110110000:
        pll_mult = s->rcc_pllcfgr.plln;
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Invalid PLLN\n", __func__);
        return;
    }

    /* Resolve PLL output division factor */
    switch (s->rcc_pllcfgr.pllp) {
    case 0b00:
        pll_postdiv = 2;
        break;
    case 0b01:
        pll_postdiv = 4;
        break;
    case 0b10:
        pll_postdiv = 6;
        break;
    case 0b11:
        pll_postdiv = 8;
        break;
    }

    /* Compute PLL output frequency */
    pll_freq = pll_src / pll_prediv * pll_mult / pll_postdiv;

    /* Resolve SYSCLK frequency */
    switch (s->rcc_cfgr.sw) {
    case 0b00: /* High-speed internal oscillator (fixed at 16MHz) */
        sysclk_freq = HSI_FREQ;
        break;
    case 0b01: /* High-speed external oscillator */
        sysclk_freq = s->hse_frequency;
        break;
    case 0b10: /* PLL */
        sysclk_freq = pll_freq;
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Invalid system clock source selected\n", __func__);
        return;
    }

    /* Resolve AHB prescaler division ratio */
    switch (s->rcc_cfgr.hpre) {
    case 0b0000 ... 0b0111:
        ahb_div = 1;
        break;
    case 0b1000:
        ahb_div = 2;
        break;
    case 0b1001:
        ahb_div = 4;
        break;
    case 0b1010:
        ahb_div = 8;
        break;
    case 0b1011:
        ahb_div = 16;
        break;
    case 0b1100:
        ahb_div = 64;
        break;
    case 0b1101:
        ahb_div = 128;
        break;
    case 0b1110:
        ahb_div = 256;
        break;
    case 0b1111:
        ahb_div = 512;
        break;
    }

    /* Compute AHB frequency */
    ahb_freq = sysclk_freq / ahb_div;

    /* Compute and set Cortex-M SysTick timer clock frequency */
    systick_freq = ahb_freq / 8;
    system_clock_scale = 1000000000 / systick_freq;
}

static void stm32f4xx_rcc_reset(DeviceState *dev)
{
    STM32F4xxRccState *s = STM32F4XX_RCC(dev);

    /* Initialise register values */
    s->rcc_cr.reg = 0x00000083;
    s->rcc_pllcfgr.reg = 0x24003010;
    s->rcc_cfgr.reg = 0x00000000;
    s->rcc_cir.reg = 0x00000000;
    s->rcc_ahb1rstr.reg = 0x00000000;
    s->rcc_ahb2rstr.reg = 0x00000000;
    s->rcc_ahb3rstr.reg = 0x00000000;
    s->rcc_apb1rstr.reg = 0x00000000;
    s->rcc_apb2rstr.reg = 0x00000000;
    s->rcc_ahb1enr.reg = 0x00100000;
    s->rcc_ahb2enr.reg = 0x00000000;
    s->rcc_ahb3enr.reg = 0x00000000;
    s->rcc_apb1enr.reg = 0x00000000;
    s->rcc_apb2enr.reg = 0x00000000;
    s->rcc_ahb1lpenr.reg = 0x7E6791FF;
    s->rcc_ahb2lpenr.reg = 0x000000F1;
    s->rcc_ahb3lpenr.reg = 0x00000001;
    s->rcc_apb1lpenr.reg = 0x36FEC9FF;
    s->rcc_apb2lpenr.reg = 0x00075F33;
    s->rcc_bdcr.reg = 0x00000000;
    s->rcc_csr.reg = 0x0E000000;
    s->rcc_sscgr.reg = 0x00000000;
    s->rcc_plli2scfgr.reg = 0x20003000;

    /* Update clock based on the reset state */
    rcc_update_clock(s);
}

static void rcc_cr_write(STM32F4xxRccState *s, RccCrType val)
{
    /* Set internal high-speed clock state */
    s->rcc_cr.hsirdy = s->rcc_cr.hsion = val.hsion;
    /* Set external high-speed clock state */
    s->rcc_cr.hserdy = s->rcc_cr.hseon = val.hseon;
    /* Set external clock bypass state */
    s->rcc_cr.hsebyp = s->rcc_cr.hserdy && val.hsebyp;
    /* Set PLL state */
    s->rcc_cr.pllrdy = s->rcc_cr.pllon = val.pllon;
    /* Set I2S PLL state */
    s->rcc_cr.plli2srdy = s->rcc_cr.plli2son = val.plli2son;

    /* Update clock */
    rcc_update_clock(s);
}

static void rcc_pllcfgr_write(STM32F4xxRccState *s, RccPllcfgrType val)
{
    /* Set PLL entry clock source */
    s->rcc_pllcfgr.pllsrc = val.pllsrc;
    /* Set main PLL input division factor */
    s->rcc_pllcfgr.pllm = val.pllm;
    /* Set main PLL multiplication factor for VCO */
    s->rcc_pllcfgr.plln = val.plln;
    /* Set main PLL output division factor */
    s->rcc_pllcfgr.pllp = val.pllp;

    /* Update clock */
    rcc_update_clock(s);
}

static void rcc_cfgr_write(STM32F4xxRccState *s, RccCfgrType val)
{
    /* Set clock switch status */
    s->rcc_cfgr.sw = s->rcc_cfgr.sws = val.sw;
    /* Set AHB prescaler clock division factor */
    s->rcc_cfgr.hpre = val.hpre;

    /* Update clock */
    rcc_update_clock(s);
}

static void rcc_csr_write(STM32F4xxRccState *s, RccCsrType val)
{
    /* Set internal low-speed oscillator state */
    s->rcc_csr.lsirdy = s->rcc_csr.lsion = val.lsion;

    /* Update clock */
    rcc_update_clock(s);
}

static uint64_t stm32f4xx_rcc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F4xxRccState *s = opaque;

    trace_stm32f4xx_rcc_read(addr);

    switch (addr) {
    case RCC_CR:
        return s->rcc_cr.reg;
    case RCC_PLLCFGR:
        return s->rcc_pllcfgr.reg;
    case RCC_CFGR:
        return s->rcc_cfgr.reg;
    case RCC_CIR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Clock interrupt configuration is not supported in QEMU\n",
            __func__);
        return s->rcc_cir.reg;
    case RCC_AHB1RSTR:
        return s->rcc_ahb1rstr.reg;
    case RCC_AHB2RSTR:
        return s->rcc_ahb2rstr.reg;
    case RCC_AHB3RSTR:
        return s->rcc_ahb3rstr.reg;
    case RCC_APB1RSTR:
        return s->rcc_apb1rstr.reg;
    case RCC_APB2RSTR:
        return s->rcc_apb2rstr.reg;
    case RCC_AHB1ENR:
        return s->rcc_ahb1enr.reg;
    case RCC_AHB2ENR:
        return s->rcc_ahb2enr.reg;
    case RCC_AHB3ENR:
        return s->rcc_ahb3enr.reg;
    case RCC_APB1ENR:
        return s->rcc_apb1enr.reg;
    case RCC_APB2ENR:
        return s->rcc_apb2enr.reg;
    case RCC_AHB1LPENR:
        return s->rcc_ahb1lpenr.reg;
    case RCC_AHB2LPENR:
        return s->rcc_ahb2lpenr.reg;
    case RCC_AHB3LPENR:
        return s->rcc_ahb3lpenr.reg;
    case RCC_APB1LPENR:
        return s->rcc_apb1lpenr.reg;
    case RCC_APB2LPENR:
        return s->rcc_apb2lpenr.reg;
    case RCC_BDCR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Backup domain control is not supported in QEMU\n",
            __func__);
        return s->rcc_bdcr.reg;
    case RCC_CSR:
        return s->rcc_csr.reg;
    case RCC_SSCGR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Spread spectrum clock generation is not supported in QEMU\n",
            __func__);
        return s->rcc_sscgr.reg;
    case RCC_PLLI2SCFGR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: PLLI2S configuration is not supported in QEMU\n",
            __func__);
        return s->rcc_plli2scfgr.reg;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }
}

static void stm32f4xx_rcc_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    STM32F4xxRccState *s = opaque;
    uint32_t value = val64;

    trace_stm32f4xx_rcc_write(value, addr);

    switch (addr) {
    case RCC_CR:
        rcc_cr_write(s, (RccCrType)value);
        return;
    case RCC_PLLCFGR:
        rcc_pllcfgr_write(s, (RccPllcfgrType)value);
        return;
    case RCC_CFGR:
        rcc_cfgr_write(s, (RccCfgrType)value);
        return;
    case RCC_CIR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Clock interrupt configuration is not supported in QEMU\n",
            __func__);
        return;
    case RCC_AHB1RSTR ... RCC_APB2RSTR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Peripheral reset is a no-op in QEMU\n", __func__);
        return;
    case RCC_AHB1ENR:
        /* Store peripheral enable status; otherwise, this is no-op */
        s->rcc_ahb1enr.reg = value;
        return;
    case RCC_AHB2ENR:
        s->rcc_ahb2enr.reg = value;
        return;
    case RCC_AHB3ENR:
        s->rcc_ahb3enr.reg = value;
        return;
    case RCC_APB1ENR:
        s->rcc_apb1enr.reg = value;
        return;
    case RCC_APB2ENR:
        s->rcc_apb2enr.reg = value;
        return;
    case RCC_AHB1LPENR:
        /* Store peripheral low power status; otherwise, this is no-op */
        s->rcc_ahb1lpenr.reg = value;
        return;
    case RCC_AHB2LPENR:
        s->rcc_ahb2lpenr.reg = value;
        return;
    case RCC_AHB3LPENR:
        s->rcc_ahb3lpenr.reg = value;
        return;
    case RCC_APB1LPENR:
        s->rcc_apb1lpenr.reg = value;
        return;
    case RCC_APB2LPENR:
        s->rcc_apb2lpenr.reg = value;
        return;
    case RCC_BDCR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Backup domain control is not supported in QEMU\n",
            __func__);
        return;
    case RCC_CSR:
        rcc_csr_write(s, (RccCsrType)value);
        return;
    case RCC_SSCGR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: Spread spectrum clock generation is not supported in QEMU\n",
            __func__);
        return;
    case RCC_PLLI2SCFGR:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: PLLI2S configuration is not supported in QEMU\n",
            __func__);
        return;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f4xx_rcc_ops = {
    .read = stm32f4xx_rcc_read,
    .write = stm32f4xx_rcc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f4xx_rcc_init(Object *obj)
{
    STM32F4xxRccState *s = STM32F4XX_RCC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_rcc_ops, s,
                          TYPE_STM32F4XX_RCC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_stm32f4xx_rcc = {
    .name = TYPE_STM32F4XX_RCC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rcc_cr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_pllcfgr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_cfgr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_cir.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb1rstr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb2rstr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb3rstr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb1rstr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb2rstr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb1enr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb2enr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb3enr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb1enr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb2enr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb1lpenr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb2lpenr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_ahb3lpenr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb1lpenr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_apb2lpenr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_bdcr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_csr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_sscgr.reg, STM32F4xxRccState),
        VMSTATE_UINT32(rcc_plli2scfgr.reg, STM32F4xxRccState),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm32f4xx_rcc_properties[] = {
    DEFINE_PROP_UINT32("hse-frequency", STM32F4xxRccState, hse_frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32f4xx_rcc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f4xx_rcc_reset;
    dc->vmsd = &vmstate_stm32f4xx_rcc;
    device_class_set_props(dc, stm32f4xx_rcc_properties);
}

static const TypeInfo stm32f4xx_rcc_info = {
    .name          = TYPE_STM32F4XX_RCC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F4xxRccState),
    .instance_init = stm32f4xx_rcc_init,
    .class_init    = stm32f4xx_rcc_class_init,
};

static void stm32f4xx_rcc_register_types(void)
{
    type_register_static(&stm32f4xx_rcc_info);
}

type_init(stm32f4xx_rcc_register_types)
