#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"

#define DATA_OFFSET 0x00
#define CTL_SPIEN   0x01
#define CTL_OFFSET  0x02
#define CTL_MOD     0x04

typedef struct PSPI {
    uint64_t base_addr;
} PSPI;

PSPI pspi_defs = {
    .base_addr  = 0xf0201000
};

static uint16_t pspi_read_data(QTestState *qts, const PSPI *pspi)
{
    return qtest_readw(qts, pspi->base_addr + DATA_OFFSET);
}

static void pspi_write_data(QTestState *qts, const PSPI *pspi, uint16_t value)
{
    qtest_writew(qts, pspi->base_addr + DATA_OFFSET, value);
}

static uint32_t pspi_read_ctl(QTestState *qts, const PSPI *pspi)
{
    return qtest_readl(qts, pspi->base_addr + CTL_OFFSET);
}

static void pspi_write_ctl(QTestState *qts, const PSPI *pspi, uint32_t value)
{
    qtest_writel(qts, pspi->base_addr + CTL_OFFSET, value);
}

/* Check PSPI can be reset to default value */
static void test_init(gconstpointer pspi_p)
{
    const PSPI *pspi = pspi_p;

    QTestState *qts = qtest_init("-machine npcm845-evb");
    pspi_write_ctl(qts, pspi, CTL_SPIEN);
    g_assert_cmphex(pspi_read_ctl(qts, pspi), ==, CTL_SPIEN);

    qtest_quit(qts);
}

/* Check PSPI can be r/w data register */
static void test_data(gconstpointer pspi_p)
{
    const PSPI *pspi = pspi_p;
    uint16_t test = 0x1234;
    uint16_t output;

    QTestState *qts = qtest_init("-machine npcm845-evb");

    /* Write to data register */
    pspi_write_data(qts, pspi, test);
    printf("Wrote 0x%x to data register\n", test);

    /* Read from data register */
    output = pspi_read_data(qts, pspi);
    printf("Read 0x%x from data register\n", output);

    qtest_quit(qts);
}

/* Check PSPI can be r/w control register */
static void test_ctl(gconstpointer pspi_p)
{
    const PSPI *pspi = pspi_p;
    uint8_t control = CTL_MOD;

    QTestState *qts = qtest_init("-machine npcm845-evb");

    /* Write CTL_MOD value to control register for 16-bit interface mode */
    qtest_memwrite(qts, pspi->base_addr + CTL_OFFSET,
                   &control, sizeof(control));
    g_assert_cmphex(pspi_read_ctl(qts, pspi), ==, control);
    printf("Wrote CTL_MOD to control register\n");

    qtest_quit(qts);
}

static void pspi_add_test(const char *name, const PSPI* wd,
        GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf("npcm8xx_pspi/%s",  name);
    qtest_add_data_func(full_name, wd, fn);
}

#define add_test(name, td) pspi_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_test(init, &pspi_defs);
    add_test(ctl, &pspi_defs);
    add_test(data, &pspi_defs);
    return g_test_run();
}
