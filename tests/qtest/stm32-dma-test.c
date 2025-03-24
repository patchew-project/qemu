// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QTest testcase for STM32 DMA engine.
 *
 * This includes STM32F1xxxx, STM32F2xxxx and GD32F30x
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"
#include "libqos/libqos.h"

/* Offsets in stm32vldiscovery platform: */
#define DMA_BASE    0x40020000
#define SRAM_BASE   0x20000000

/* Global interrupt flag */
#define DMA_ISR_GIF   BIT(0)
/* Full transfer finish */
#define DMA_ISR_TCIF  BIT(1)
/* Half transfer finish */
#define DMA_ISR_HTIF  BIT(2)
/* Transfer error */
#define DMA_ISR_TEIF  BIT(3)

/* Used register/fields definitions */
#define DMA_CCR(idx)     (0x08 + 0x14 * idx)
#define DMA_CNDTR(idx)   (0x0C + 0x14 * idx)
#define DMA_CPAR(idx)    (0x10 + 0x14 * idx)
#define DMA_CMAR(idx)    (0x14 + 0x14 * idx)

#define DMA_MAX_CHAN    7

/* Register offsets for a dma chan0 within a dma block. */
#define DMA_CHAN(_idx, _irq)  \
    { \
        .ccr = DMA_CCR(_idx), \
        .cndrt = DMA_CNDTR(_idx), \
        .cpar = DMA_CPAR(_idx), \
        .cmar = DMA_CMAR(_idx), \
        .irq_line = _irq,\
    }

typedef struct DMAChan {
    uint32_t ccr;
    uint32_t cndrt;
    uint32_t cpar;
    uint32_t cmar;
    uint8_t irq_line;
} DMAChan;

const DMAChan dma_chans[] = {
    DMA_CHAN(0, 11),
    DMA_CHAN(1, 12),
    DMA_CHAN(2, 12),
    DMA_CHAN(3, 13),
    DMA_CHAN(4, 14),
    DMA_CHAN(5, 16),
    DMA_CHAN(6, 17),
};

/* Register offsets for a dma within a dma block. */
typedef struct DMA {
    uint32_t base_addr;
    uint32_t isr;
    uint32_t ofcr;
} DMA;

const DMA dma = {
    .base_addr = DMA_BASE,
    .isr = 0x00,
    .ofcr = 0x04,
};

typedef struct TestData {
    QTestState *qts;
    const DMA *dma;
    const DMAChan *chans;
} TestData;

#define NVIC_ISER 0xE000E100
#define NVIC_ISPR 0xE000E200
#define NVIC_ICPR 0xE000E280

static void enable_nvic_irq(unsigned int n)
{
    writel(NVIC_ISER, 1 << n);
}

static void unpend_nvic_irq(unsigned int n)
{
    writel(NVIC_ICPR, 1 << n);
}

static bool check_nvic_pending(unsigned int n)
{
    return readl(NVIC_ISPR) & (1 << n);
}

static uint32_t dma_read(const TestData *td, uint32_t offset)
{
    return qtest_readl(td->qts, td->dma->base_addr + offset);
}

static void dma_write(const TestData *td, uint32_t offset, uint32_t value)
{
    qtest_writel(td->qts, td->dma->base_addr + offset, value);
}

static void dma_write_ofcr(const TestData *td, uint32_t value)
{
    return dma_write(td, td->dma->ofcr, value);
}

static uint32_t dma_read_isr(const TestData *td)
{
    return dma_read(td, td->dma->isr);
}

static void dma_write_ccr(const TestData *td, uint8_t idx, uint32_t value)
{
    dma_write(td, td->chans[idx].ccr, value);
}

static uint32_t dma_read_ccr(const TestData *td, uint8_t idx)
{
    return dma_read(td, td->chans[idx].ccr);
}

static void dma_write_cndrt(const TestData *td, uint8_t idx, uint32_t value)
{
    dma_write(td, td->chans[idx].cndrt, value);
}

static void dma_write_cpar(const TestData *td, uint8_t idx, uint32_t value)
{
    dma_write(td, td->chans[idx].cpar, value);
}

static void dma_write_cmar(const TestData *td, uint8_t idx, uint32_t value)
{
    dma_write(td, td->chans[idx].cmar, value);
}

static void test_m2m(gconstpointer test_data)
{
    const TestData *td = test_data;
    QTestState *s = td->qts;
    const uint32_t patt_len = 0xff;
    char *pattern_check = g_malloc(patt_len);
    char *pattern = g_malloc(patt_len);
    uint8_t idx = 0;
    uint32_t val;

    enable_nvic_irq(td->chans[idx].irq_line);
    qtest_irq_intercept_in(global_qtest, "/machine/soc/dma[0]");

    /* write addr */
    dma_write_cpar(td, idx, SRAM_BASE);
    dma_write_cmar(td, idx, SRAM_BASE + patt_len);

    /* enable increment and M2M */
    val = dma_read_ccr(td, idx);
    val |= BIT(1); /* TCIE */
    val |= BIT(6); /* PINC */
    val |= BIT(7); /* MINC */
    val |= BIT(14); /* M2M */
    dma_write_ccr(td, idx, val);

    generate_pattern(pattern, patt_len, patt_len);
    qtest_memwrite(s, SRAM_BASE, pattern, patt_len);

    dma_write_cndrt(td, idx, patt_len);

    val |= BIT(0); /* enable channel */
    dma_write_ccr(td, idx, val);

    qtest_memread(s, SRAM_BASE + patt_len, pattern_check, patt_len);

    g_assert(memcmp(pattern, pattern_check, patt_len) == 0);

    g_assert_true(check_nvic_pending(td->chans[idx].irq_line));
}

typedef struct width_pattern {
    uint32_t src;
    uint8_t swidth;
    uint32_t dst;
    uint8_t dwidth;
} width_pattern;

static void test_width(gconstpointer test_data)
{
    const width_pattern patterns[] = {
        { 0xb0,       1, 0xb0,       1 },
        { 0xb0,       1, 0x00b0,     2 },
        { 0xb0,       1, 0x000000b0, 4 },
        { 0xb1b0,     2, 0xb0,       1 },
        { 0xb1b0,     2, 0xb1b0,     2 },
        { 0xb1b0,     2, 0x0000b1b0, 4 },
        { 0xb3b2b1b0, 4, 0xb0,       1 },
        { 0xb3b2b1b0, 4, 0xb1b0,     2 },
        { 0xb3b2b1b0, 4, 0xb3b2b1b0, 4 },
    };

    const TestData *td = test_data;
    QTestState *s = td->qts;
    const uint32_t patt = 0xffffffff;
    const uint32_t patt_len = 4;
    uint32_t dst;
    uint8_t idx = 0;
    uint32_t val;

    qmp("{'execute':'system_reset' }");

    /* write addr */
    dma_write_cpar(td, idx, SRAM_BASE);
    dma_write_cmar(td, idx, SRAM_BASE + patt_len);

    /* enable increment and M2M */
    val = dma_read_ccr(td, idx);
    val |= BIT(6); /* PINC */
    val |= BIT(7); /* MINC */
    val |= BIT(14); /* M2M */
    dma_write_ccr(td, idx, val);

    for (int i = 0; i < ARRAY_SIZE(patterns); i++) {
        /* fill destination and source with pattern */
        qtest_memwrite(s, SRAM_BASE, &patt, patt_len);
        qtest_memwrite(s, SRAM_BASE + patt_len, &patt, patt_len);

        qtest_memwrite(s, SRAM_BASE, &patterns[i].src, patterns[i].swidth);

        dma_write_cndrt(td, idx, 1);
        val |= BIT(0); /* enable channel */
        val = deposit32(val, 8, 2, patterns[i].swidth >> 1);
        val = deposit32(val, 10, 2, patterns[i].dwidth >> 1);
        dma_write_ccr(td, idx, val);

        qtest_memread(s, SRAM_BASE + patt_len, &dst, patterns[i].dwidth);

        g_assert(memcmp(&dst, &patterns[i].dst, patterns[i].dwidth) == 0);

        /* disable chan */
        val &= ~BIT(0);
        dma_write_ccr(td, idx, val);
    }
}

static void dma_set_irq(unsigned int idx, int num, int level)
{
    g_autofree char *name = g_strdup_printf("/machine/soc/dma[%u]",
                                            idx);
    qtest_set_irq_in(global_qtest, name, NULL, num, level);
}

static void test_triggers(gconstpointer test_data)
{
    const TestData *td = test_data;
    QTestState *s = td->qts;
    const uint32_t patt = 0xffffffff;
    const uint32_t patt_len = 4;
    uint32_t dst;
    uint32_t val;

    qmp("{'execute':'system_reset' }");

    for (int i = 0; i < ARRAY_SIZE(dma_chans); i++) {
        qtest_memset(s, SRAM_BASE, 0, patt_len * 2);
        qtest_memwrite(s, SRAM_BASE, &patt, patt_len);

        /* write addr */
        dma_write_cpar(td, i, SRAM_BASE);
        dma_write_cmar(td, i, SRAM_BASE + patt_len);

        val = dma_read_ccr(td, i);

        dma_write_cndrt(td, i, 1);
        val |= BIT(0); /* enable channel */
        val = deposit32(val, 8, 2, patt_len >> 1);
        val = deposit32(val, 10, 2, patt_len >> 1);
        dma_write_ccr(td, i, val);

        dma_set_irq(0, i, 1);

        qtest_memread(s, SRAM_BASE + patt_len, &dst, patt_len);

        g_assert(memcmp(&dst, &patt, patt_len) == 0);

        /* disable chan */
        val &= ~BIT(0);
        dma_write_ccr(td, i, val);
    }
}

static void test_interrupts(gconstpointer test_data)
{
    const TestData *td = test_data;
    const uint32_t patt_len = 1024;
    uint8_t idx = 0;
    uint32_t val;

    qmp("{'execute':'system_reset' }");

    enable_nvic_irq(td->chans[idx].irq_line);

    /* write addr */
    dma_write_cpar(td, idx, SRAM_BASE);
    dma_write_cmar(td, idx, SRAM_BASE + patt_len);

    /* write counter */
    dma_write_cndrt(td, idx, 2);

    /* enable increment and M2M */
    val = dma_read_ccr(td, idx);
    val |= BIT(0); /* EN */
    val |= BIT(1); /* TCIE */
    val |= BIT(2); /* HTIE */
    val |= BIT(3); /* TEIE */
    val |= BIT(6); /* PINC */
    val |= BIT(7); /* MINC */
    dma_write_ccr(td, idx, val);

    /* Half-transfer */
    dma_set_irq(0, idx, 1);
    g_assert_true(check_nvic_pending(td->chans[idx].irq_line));
    val = dma_read_isr(td);

    g_assert_true(val & DMA_ISR_GIF);
    g_assert_true(val & DMA_ISR_HTIF);
    unpend_nvic_irq(td->chans[idx].irq_line);

    dma_write_ofcr(td, 0xffffffff);
    val = dma_read_isr(td);
    g_assert_false(val & DMA_ISR_GIF);
    g_assert_false(val & DMA_ISR_HTIF);

    /* Full-transfer */
    dma_set_irq(0, idx, 1);
    g_assert_true(check_nvic_pending(td->chans[idx].irq_line));
    val = dma_read_isr(td);

    g_assert_true(val & DMA_ISR_GIF);
    g_assert_true(val & DMA_ISR_HTIF);
    g_assert_true(val & DMA_ISR_TCIF);
    unpend_nvic_irq(td->chans[idx].irq_line);

    dma_write_ofcr(td, 0xffffffff);
    val = dma_read_isr(td);
    g_assert_false(val & DMA_ISR_GIF);
    g_assert_false(val & DMA_ISR_HTIF);
    g_assert_false(val & DMA_ISR_TCIF);

    /* Error-on-transfer */
    val = dma_read_ccr(td, idx);
    val &= ~BIT(0);
    dma_write_ccr(td, idx, val);

    dma_write_cndrt(td, idx, 1);
    dma_write_cpar(td, idx, 0xffffffff);

    val |= BIT(0);
    dma_write_ccr(td, idx, val);

    dma_set_irq(0, idx, 1);
    g_assert_true(check_nvic_pending(td->chans[idx].irq_line));
    val = dma_read_isr(td);

    g_assert_true(val & DMA_ISR_GIF);
    g_assert_true(val & DMA_ISR_TEIF);
    unpend_nvic_irq(td->chans[idx].irq_line);

    dma_write_ofcr(td, 0xffffffff);
    val = dma_read_isr(td);
    g_assert_false(val & DMA_ISR_GIF);
    g_assert_false(val & DMA_ISR_TEIF);
}

static void stm32_add_test(const char *name, const TestData *td,
                           GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
        "stm32_dma/%s", name);
    qtest_add_data_func(full_name, td, fn);
}

/* Convenience macro for adding a test with a predictable function name. */
#define add_test(name, td) stm32_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    TestData testdata;
    QTestState *s;
    int ret;

    g_test_init(&argc, &argv, NULL);
    s = qtest_start("-machine stm32vldiscovery");
    g_test_set_nonfatal_assertions();

    TestData *td = &testdata;
    td->qts = s;
    td->dma = &dma;
    td->chans = dma_chans;
    add_test(m2m, td);
    add_test(width, td);
    add_test(triggers, td);
    add_test(interrupts, td);

    ret = g_test_run();
    qtest_end();

    return ret;
}
