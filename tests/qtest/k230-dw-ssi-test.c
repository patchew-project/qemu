/*
 * QTest for the Kendryte K230 DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/units.h"

#define K230_SPI0_BASE          0x91584000ULL
#define K230_SPI1_BASE          0x91582000ULL
#define K230_SPI2_BASE          0x91583000ULL
#define K230_PLIC_BASE          0xf00000000ULL
#define K230_PLIC_PENDING_BASE  0x1000
#define K230_SSI_CTRLR0          0x000
#define K230_SSI_CTRLR1          0x004
#define K230_SSI_SSIENR          0x008
#define K230_SSI_SER             0x010
#define K230_SSI_BAUDR           0x014
#define K230_SSI_TXFTLR          0x018
#define K230_SSI_TXFLR           0x020
#define K230_SSI_RXFLR           0x024
#define K230_SSI_SR              0x028
#define K230_SSI_IMR             0x02c
#define K230_SSI_ISR             0x030
#define K230_SSI_RISR            0x034
#define K230_SSI_RXUICR          0x040
#define K230_SSI_DMACR           0x04c
#define K230_SSI_IDR             0x058
#define K230_SSI_VERSION_ID      0x05c
#define K230_SSI_DR0             0x060
#define K230_SSI_SPI_CTRLR0      0x0f4
#define K230_SSI_XIP_MODE_BITS   0x0fc
#define K230_SSI_XIP_INCR_INST   0x100
#define K230_SSI_SPIDR           0x120
#define K230_SSI_SPIAR           0x124
#define K230_SSI_AXIAR0          0x128
#define K230_SSI_AXIAR1          0x12c
#define K230_SSI_AXIECR          0x130
#define K230_SSI_DONECR          0x134

#define K230_SSI_CTRLR0_RESET           0x00004007U
#define K230_SSI_SPI_CTRLR0_SPI_RESET   0x04000200U
#define K230_SSI_SPI_CTRLR0_FMC_RESET   0x28000200U
#define K230_SSI_IMR_RESET              0x0000003fU
#define K230_SSI_IDR_RESET              0xa1b2c3d5U
#define K230_SSI_VERSION_RESET          0x3130332aU

#define K230_SSI_CTRLR0_WRITABLE_MASK   0x01cf7f1fU
#define K230_SSI_BAUDR_WRITABLE_MASK    0x0000fffeU

#define K230_SSI_CTRLR0_DFS_MASK        0x1fU
#define K230_SSI_CTRLR0_TMOD_SHIFT      10
#define K230_SSI_CTRLR0_SRL             BIT(13)
#define K230_SSI_CTRLR0_SPI_FRF_SHIFT   22
#define K230_SSI_CTRLR0_SPI_FRF_MASK    (3U << K230_SSI_CTRLR0_SPI_FRF_SHIFT)

#define K230_SSI_TMOD_TR                0
#define K230_SSI_TMOD_TO                1
#define K230_SSI_TMOD_RO                2
#define K230_SSI_TMOD_EEPROM_READ       3

#define K230_SSI_FRF_QUAD               2
#define K230_SSI_FRF_OCTAL              3

#define K230_SSI_SPI_CTRLR0_TRANS_TYPE(v) ((v) & 0x3U)
#define K230_SSI_SPI_CTRLR0_ADDR_L(bits)  (((bits) / 4U) << 2)
#define K230_SSI_SPI_CTRLR0_XIP_MD_EN     BIT(7)
#define K230_SSI_SPI_CTRLR0_INST_L_8      (2U << 8)
#define K230_SSI_SPI_CTRLR0_WAIT(v)       (((v) & 0x1fU) << 11)
#define K230_SSI_SPI_CTRLR0_SPI_DDR_EN    BIT(16)
#define K230_SSI_SPI_CTRLR0_XIP_MBL_8     (2U << 26)

#define K230_SSI_SR_BUSY                BIT(0)
#define K230_SSI_SR_TFNF                BIT(1)
#define K230_SSI_SR_TFE                 BIT(2)
#define K230_SSI_SR_RFNE                BIT(3)
#define K230_SSI_SR_CMPLTD_DF_SHIFT     15
#define K230_SSI_SR_CMPLTD_DF_MASK      (0x1ffffU << 15)

#define K230_SSI_INT_TXE                BIT(0)
#define K230_SSI_INT_RXU                BIT(2)
#define K230_SSI_INT_AXIE               BIT(8)
#define K230_SSI_INT_DONE               BIT(11)

#define K230_SSI_IDMAE                  BIT(2)
#define K230_SSI_AINC                   BIT(6)
#define K230_SSI_DMA_ADDR               0x80201000ULL

#define K230_SSI_IRQ_TXE                0
#define K230_SSI_IRQ_RXU                5
#define K230_SSI_IRQ_DONE               7
#define K230_SSI_IRQ_AXIE               8

#define K230_SSI_FIFO_DEPTH             256
#define K230_SSI_FLASH_IMAGE_SIZE       (32 * MiB)
#define K230_SSI_FLASH_PATTERN_ADDR     0x100
#define K230_SSI_FLASH_HIGH_ADDR        0x1000100
#define K230_SSI_FLASH_PROGRAM_ADDR     0x22000

typedef struct K230SsiInstance {
    uint64_t base;
    uint32_t num_cs;
    uint32_t spi_ctrlr0_reset;
    uint32_t first_irq;
} K230SsiInstance;

typedef struct K230SsiFlashImage {
    char *path;
} K230SsiFlashImage;

static const K230SsiInstance k230_ssi_instances[3] = {
    {
        .base = K230_SPI0_BASE,
        .num_cs = 1,
        .spi_ctrlr0_reset = K230_SSI_SPI_CTRLR0_FMC_RESET,
        .first_irq = 146,
    }, {
        .base = K230_SPI1_BASE,
        .num_cs = 5,
        .spi_ctrlr0_reset = K230_SSI_SPI_CTRLR0_SPI_RESET,
        .first_irq = 155,
    }, {
        .base = K230_SPI2_BASE,
        .num_cs = 5,
        .spi_ctrlr0_reset = K230_SSI_SPI_CTRLR0_SPI_RESET,
        .first_irq = 164,
    },
};

#define FLASH_CMD_JEDEC         0x9f

#define FLASH_CMD_WREN          0x06
#define FLASH_CMD_RDSR          0x05
#define FLASH_CMD_READ          0x03
#define FLASH_CMD_READ4         0x13
#define FLASH_CMD_QUAD_OUT      0x6b
#define FLASH_CMD_QUAD_IO       0xeb
#define FLASH_CMD_PP            0x02
#define FLASH_SR_WIP            BIT(0)

#define FLASH_CMD_QUAD_PP       0x32

static QTestState *k230_ssi_start(void)
{
    return qtest_init("-machine k230");
}

static uint32_t k230_ssi_readl(QTestState *qts, uint64_t base,
                               uint32_t offset)
{
    return qtest_readl(qts, base + offset);
}

static void k230_ssi_writel(QTestState *qts, uint64_t base,
                            uint32_t offset, uint32_t value)
{
    qtest_writel(qts, base + offset, value);
}

static void k230_ssi_disable(QTestState *qts, uint64_t base)
{
    k230_ssi_writel(qts, base, K230_SSI_SSIENR, 0);
}

static void k230_ssi_configure(QTestState *qts, uint64_t base,
                               uint32_t tmod, uint32_t dfs_bits,
                               uint32_t ndf)
{
    uint32_t ctrlr0;

    g_assert_cmpuint(dfs_bits, >=, 4);
    g_assert_cmpuint(dfs_bits, <=, 32);
    g_assert_cmpuint(tmod, <=, K230_SSI_TMOD_EEPROM_READ);

    k230_ssi_disable(qts, base);
    ctrlr0 = (dfs_bits - 1) & K230_SSI_CTRLR0_DFS_MASK;
    ctrlr0 |= tmod << K230_SSI_CTRLR0_TMOD_SHIFT;
    k230_ssi_writel(qts, base, K230_SSI_CTRLR0, ctrlr0);
    k230_ssi_writel(qts, base, K230_SSI_CTRLR1, ndf);
    k230_ssi_writel(qts, base, K230_SSI_BAUDR, 2);
}

static void k230_ssi_enable_cs(QTestState *qts, uint64_t base, uint32_t ser)
{
    k230_ssi_writel(qts, base, K230_SSI_SER, ser);
    k230_ssi_writel(qts, base, K230_SSI_SSIENR, 1);
}

static void k230_ssi_write_frame(QTestState *qts, uint64_t base,
                                 uint32_t value)
{
    k230_ssi_writel(qts, base, K230_SSI_DR0, value);
}

static uint32_t k230_ssi_read_frame(QTestState *qts, uint64_t base)
{
    return k230_ssi_readl(qts, base, K230_SSI_DR0);
}

static void k230_ssi_wait_mask(QTestState *qts, uint64_t base,
                               uint32_t offset, uint32_t mask,
                               uint32_t expected)
{
    for (int i = 0; i < 1000; i++) {
        uint32_t value = k230_ssi_readl(qts, base, offset);

        if ((value & mask) == expected) {
            return;
        }
        qtest_clock_step(qts, 1000);
    }

    g_assert_cmphex(k230_ssi_readl(qts, base, offset) & mask,
                    ==, expected);
}

static void configure_loopback(QTestState *qts, uint32_t tmod,
                               uint32_t ndf)
{
    uint32_t ctrlr0;

    k230_ssi_configure(qts, K230_SPI1_BASE, tmod, 8, ndf);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0,
                    ctrlr0 | K230_SSI_CTRLR0_SRL);
}

static bool k230_ssi_plic_pending(QTestState *qts, uint32_t irq)
{
    uint64_t addr = K230_PLIC_BASE + K230_PLIC_PENDING_BASE +
                    (irq / 32) * sizeof(uint32_t);

    return qtest_readl(qts, addr) & BIT(irq % 32);
}

static void write_exact(int fd, const void *buf, size_t len, off_t offset)
{
    ssize_t ret = pwrite(fd, buf, len, offset);

    g_assert_cmpint(ret, ==, len);
}

static void k230_ssi_flash_image_init(K230SsiFlashImage *image)
{
    static const uint8_t low_pattern[] = {
        0xa5, 0x5a, 0x3c, 0xc3, 0x11, 0x22, 0x33, 0x44,
    };
    static const uint8_t high_pattern[] = { 0x71, 0x72, 0x73, 0x74 };
    uint8_t erased[4096];
    int fd;

    memset(erased, 0xff, sizeof(erased));
    image->path = NULL;
    fd = g_file_open_tmp("qtest.k230.w25q256.XXXXXX", &image->path, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, K230_SSI_FLASH_IMAGE_SIZE), ==, 0);
    write_exact(fd, low_pattern, sizeof(low_pattern),
                K230_SSI_FLASH_PATTERN_ADDR);
    write_exact(fd, high_pattern, sizeof(high_pattern),
                K230_SSI_FLASH_HIGH_ADDR);
    write_exact(fd, erased, sizeof(erased), K230_SSI_FLASH_PROGRAM_ADDR);
    close(fd);
}

static void k230_ssi_flash_image_clear(K230SsiFlashImage *image)
{
    if (image->path) {
        unlink(image->path);
        g_clear_pointer(&image->path, g_free);
    }
}

static QTestState *k230_ssi_start_with_flash(K230SsiFlashImage *image)
{
    k230_ssi_flash_image_init(image);
    return qtest_initf("-machine k230,spi-flash=w25q256 "
                       "-drive file=%s,format=raw,if=mtd",
                       image->path);
}

static void flash_write_transaction(QTestState *qts,
                                    const uint8_t *command,
                                    size_t command_len)
{
    g_assert_nonnull(command);
    g_assert_cmpuint(command_len, >, 0);
    g_assert_cmpuint(command_len, <=, K230_SSI_FIFO_DEPTH);

    k230_ssi_configure(qts, K230_SPI0_BASE, K230_SSI_TMOD_TO, 8, 0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, 0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SSIENR, 1);
    for (size_t i = 0; i < command_len; i++) {
        k230_ssi_write_frame(qts, K230_SPI0_BASE, command[i]);
    }
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, BIT(0));
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_TXFLR,
                       UINT32_MAX, 0);
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_SR,
                       K230_SSI_SR_BUSY, 0);
    k230_ssi_disable(qts, K230_SPI0_BASE);
}

static void flash_read_transaction(QTestState *qts,
                                   const uint8_t *command,
                                   size_t command_len,
                                   uint8_t *data, size_t data_len)
{
    g_assert_nonnull(command);
    g_assert_nonnull(data);
    g_assert_cmpuint(command_len, >, 0);
    g_assert_cmpuint(data_len, >, 0);
    g_assert_cmpuint(data_len, <, K230_SSI_FIFO_DEPTH);

    k230_ssi_configure(qts, K230_SPI0_BASE,
                       K230_SSI_TMOD_EEPROM_READ, 8, data_len - 1);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, 0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SSIENR, 1);
    for (size_t i = 0; i < command_len; i++) {
        k230_ssi_write_frame(qts, K230_SPI0_BASE, command[i]);
    }
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, BIT(0));
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = k230_ssi_read_frame(qts, K230_SPI0_BASE);
    }
    k230_ssi_disable(qts, K230_SPI0_BASE);
}

static void flash_read(QTestState *qts, uint8_t opcode, uint32_t address,
                       unsigned int addr_bytes, uint8_t *data, size_t len)
{
    uint8_t command[5];

    g_assert_cmpuint(addr_bytes, <=, 4);
    command[0] = opcode;
    for (unsigned int i = 0; i < addr_bytes; i++) {
        command[1 + i] = address >> (8 * (addr_bytes - i - 1));
    }
    flash_read_transaction(qts, command, 1 + addr_bytes, data, len);
}

static uint8_t flash_read_status(QTestState *qts)
{
    uint8_t command = FLASH_CMD_RDSR;
    uint8_t status;

    flash_read_transaction(qts, &command, 1, &status, 1);
    return status;
}

static void flash_wait_ready(QTestState *qts)
{
    for (int i = 0; i < 1000; i++) {
        if (!(flash_read_status(qts) & FLASH_SR_WIP)) {
            return;
        }
        qtest_clock_step(qts, 1000000);
    }
    g_assert_cmphex(flash_read_status(qts) & FLASH_SR_WIP, ==, 0);
}

static void flash_write_enable(QTestState *qts)
{
    uint8_t command = FLASH_CMD_WREN;

    flash_write_transaction(qts, &command, 1);
}

static void configure_enhanced_transfer(QTestState *qts, uint32_t tmod,
                                        uint32_t frf, uint32_t trans_type,
                                        uint32_t wait_cycles,
                                        bool mode_bits_enabled,
                                        size_t data_frames)
{
    uint32_t ctrlr0;
    uint32_t spi_ctrlr0;

    k230_ssi_configure(qts, K230_SPI0_BASE, tmod, 8, data_frames - 1);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, 0);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0);
    ctrlr0 &= ~K230_SSI_CTRLR0_SPI_FRF_MASK;
    ctrlr0 |= frf << K230_SSI_CTRLR0_SPI_FRF_SHIFT;
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_CTRLR0, ctrlr0);

    spi_ctrlr0 = K230_SSI_SPI_CTRLR0_TRANS_TYPE(trans_type) |
                 K230_SSI_SPI_CTRLR0_ADDR_L(24) |
                 K230_SSI_SPI_CTRLR0_INST_L_8 |
                 K230_SSI_SPI_CTRLR0_WAIT(wait_cycles);
    if (mode_bits_enabled) {
        spi_ctrlr0 |= K230_SSI_SPI_CTRLR0_XIP_MD_EN |
                      K230_SSI_SPI_CTRLR0_XIP_MBL_8;
    }
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPI_CTRLR0, spi_ctrlr0);
}

static void start_enhanced_transfer(QTestState *qts, uint8_t opcode,
                                    uint32_t address)
{
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SSIENR, 1);
    k230_ssi_write_frame(qts, K230_SPI0_BASE, opcode);
    k230_ssi_write_frame(qts, K230_SPI0_BASE, address);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, BIT(0));
}

static void read_enhanced_result(QTestState *qts, uint8_t *data, size_t len)
{
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, len);
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_SR,
                       K230_SSI_SR_BUSY, 0);
    for (size_t i = 0; i < len; i++) {
        data[i] = k230_ssi_read_frame(qts, K230_SPI0_BASE);
    }
    k230_ssi_disable(qts, K230_SPI0_BASE);
}

static void configure_idma(QTestState *qts, uint32_t tmod,
                           uint8_t opcode, uint32_t flash_address,
                           uint64_t dma_address, size_t length)
{
    uint32_t ctrlr0;
    uint32_t spi_ctrlr0;

    k230_ssi_configure(qts, K230_SPI0_BASE, tmod, 8, length - 1);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, 0);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0);
    ctrlr0 |= K230_SSI_FRF_QUAD << K230_SSI_CTRLR0_SPI_FRF_SHIFT;
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_CTRLR0, ctrlr0);
    spi_ctrlr0 = K230_SSI_SPI_CTRLR0_TRANS_TYPE(0) |
                 K230_SSI_SPI_CTRLR0_ADDR_L(24) |
                 K230_SSI_SPI_CTRLR0_INST_L_8 |
                 K230_SSI_SPI_CTRLR0_WAIT(8);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPI_CTRLR0, spi_ctrlr0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_DMACR,
                    K230_SSI_IDMAE | K230_SSI_AINC);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPIDR, opcode);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPIAR, flash_address);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_AXIAR0, dma_address);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_AXIAR1,
                    dma_address >> 32);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SSIENR, 1);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SER, BIT(0));
}

static void assert_idma_stopped(QTestState *qts, size_t completed)
{
    uint32_t status = k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_SR);

    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_SSIENR),
                    ==, 0);
    g_assert_cmpuint((status & K230_SSI_SR_CMPLTD_DF_MASK) >>
                     K230_SSI_SR_CMPLTD_DF_SHIFT, ==, completed);
}

static void test_register_contract(void)
{
    QTestState *qts = k230_ssi_start();

    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        const K230SsiInstance *inst = &k230_ssi_instances[i];

        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_CTRLR0),
                        ==, K230_SSI_CTRLR0_RESET);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_SSIENR),
                        ==, 0);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base,
                                      K230_SSI_SPI_CTRLR0),
                        ==, inst->spi_ctrlr0_reset);
        k230_ssi_writel(qts, inst->base, K230_SSI_SER, UINT32_MAX);
        g_assert_cmphex(k230_ssi_readl(qts, inst->base, K230_SSI_SER),
                        ==, MAKE_64BIT_MASK(0, inst->num_cs));
    }

    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_IDR),
                    ==, K230_SSI_IDR_RESET);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE,
                                  K230_SSI_VERSION_ID),
                    ==, K230_SSI_VERSION_RESET);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_CTRLR0, UINT32_MAX);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_CTRLR0),
                    ==, K230_SSI_CTRLR0_WRITABLE_MASK);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_BAUDR, UINT32_MAX);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_BAUDR),
                    ==, K230_SSI_BAUDR_WRITABLE_MASK);

    qtest_system_reset(qts);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0),
                    ==, K230_SSI_CTRLR0_RESET);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_IMR),
                    ==, K230_SSI_IMR_RESET);
    qtest_quit(qts);
}

static void test_pio_data_path(void)
{
    QTestState *qts = k230_ssi_start();
    uint32_t status;

    configure_loopback(qts, K230_SSI_TMOD_TR, 0);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0xa5);
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_SR,
                       K230_SSI_SR_RFNE, K230_SSI_SR_RFNE);
    g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0xa5);

    configure_loopback(qts, K230_SSI_TMOD_RO, 3);
    k230_ssi_enable_cs(qts, K230_SPI1_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI1_BASE, 0);
    k230_ssi_wait_mask(qts, K230_SPI1_BASE, K230_SSI_RXFLR,
                       UINT32_MAX, 4);
    for (int i = 0; i < 4; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI1_BASE), ==, 0);
    }

    k230_ssi_disable(qts, K230_SPI1_BASE);
    status = k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_SR);
    g_assert_cmpuint(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXFLR),
                     ==, 0);
    g_assert_cmphex(status & (K230_SSI_SR_BUSY | K230_SSI_SR_TFNF |
                              K230_SSI_SR_TFE | K230_SSI_SR_RFNE),
                    ==, K230_SSI_SR_TFNF | K230_SSI_SR_TFE);
    qtest_quit(qts);
}

static void test_interrupt_controller(void)
{
    QTestState *qts = k230_ssi_start();

    k230_ssi_configure(qts, K230_SPI1_BASE, K230_SSI_TMOD_TR, 8, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_TXFTLR, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_SSIENR, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_TXE, ==, K230_SSI_INT_TXE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_TXE, ==, 0);
    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR,
                    K230_SSI_INT_TXE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_TXE, ==, K230_SSI_INT_TXE);

    k230_ssi_writel(qts, K230_SPI1_BASE, K230_SSI_IMR,
                    K230_SSI_INT_RXU);
    (void)k230_ssi_read_frame(qts, K230_SPI1_BASE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, K230_SSI_INT_RXU);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_RXU, ==, K230_SSI_INT_RXU);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RXUICR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI1_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_RXU, ==, 0);
    qtest_quit(qts);
}

static void test_plic_routing(void)
{
    QTestState *qts = k230_ssi_start();
    const K230SsiInstance *target = &k230_ssi_instances[1];

    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        const K230SsiInstance *inst = &k230_ssi_instances[i];

        g_assert_true(k230_ssi_plic_pending(qts,
                                           inst->first_irq +
                                           K230_SSI_IRQ_TXE));
        k230_ssi_writel(qts, inst->base, K230_SSI_IMR, 0);
    }

    k230_ssi_writel(qts, target->base, K230_SSI_IMR, K230_SSI_INT_RXU);
    (void)k230_ssi_read_frame(qts, target->base);
    g_assert_true(k230_ssi_plic_pending(qts,
                                       target->first_irq +
                                       K230_SSI_IRQ_RXU));
    for (int i = 0; i < ARRAY_SIZE(k230_ssi_instances); i++) {
        if (&k230_ssi_instances[i] != target) {
            g_assert_false(k230_ssi_plic_pending(
                qts, k230_ssi_instances[i].first_irq + K230_SSI_IRQ_RXU));
        }
    }
    qtest_quit(qts);
}

static void assert_enhanced_config_rejected(QTestState *qts, uint32_t frf,
                                            uint32_t extra_spi_ctrlr0)
{
    uint32_t ctrlr0;
    uint32_t spi_ctrlr0;

    k230_ssi_configure(qts, K230_SPI0_BASE, K230_SSI_TMOD_RO, 8, 3);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0);
    ctrlr0 |= frf << K230_SSI_CTRLR0_SPI_FRF_SHIFT;
    spi_ctrlr0 = K230_SSI_SPI_CTRLR0_INST_L_8 | extra_spi_ctrlr0;
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_CTRLR0, ctrlr0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPI_CTRLR0, spi_ctrlr0);
    k230_ssi_enable_cs(qts, K230_SPI0_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI0_BASE, FLASH_CMD_JEDEC);
    g_assert_cmpuint(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_TXFLR),
                     ==, 1);
    g_assert_cmpuint(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RXFLR),
                     ==, 0);
    k230_ssi_disable(qts, K230_SPI0_BASE);
}

static void test_qspi_config(void)
{
    QTestState *qts = k230_ssi_start();
    uint32_t ctrlr0;
    uint32_t spi_ctrlr0;

    assert_enhanced_config_rejected(qts, K230_SSI_FRF_OCTAL, 0);
    assert_enhanced_config_rejected(qts, K230_SSI_FRF_QUAD,
                                    K230_SSI_SPI_CTRLR0_SPI_DDR_EN);

    k230_ssi_configure(qts, K230_SPI0_BASE, K230_SSI_TMOD_RO, 8, 3);
    ctrlr0 = k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_CTRLR0);
    ctrlr0 |= K230_SSI_FRF_QUAD << K230_SSI_CTRLR0_SPI_FRF_SHIFT;
    spi_ctrlr0 = K230_SSI_SPI_CTRLR0_TRANS_TYPE(1) |
                 K230_SSI_SPI_CTRLR0_ADDR_L(24) |
                 K230_SSI_SPI_CTRLR0_INST_L_8 |
                 K230_SSI_SPI_CTRLR0_WAIT(8);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_CTRLR0, ctrlr0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_SPI_CTRLR0, spi_ctrlr0);
    k230_ssi_enable_cs(qts, K230_SPI0_BASE, BIT(0));
    k230_ssi_write_frame(qts, K230_SPI0_BASE, FLASH_CMD_JEDEC);
    k230_ssi_write_frame(qts, K230_SPI0_BASE, 0x123456);
    g_assert_cmpuint(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RXFLR),
                     ==, 4);
    for (int i = 0; i < 4; i++) {
        g_assert_cmphex(k230_ssi_read_frame(qts, K230_SPI0_BASE), ==, 0);
    }
    qtest_quit(qts);
}

static void test_spi_nor(void)
{
    static const uint8_t expected[] = {
        0xa5, 0x5a, 0x3c, 0xc3, 0x11, 0x22, 0x33, 0x44,
    };
    static const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    K230SsiFlashImage image;
    QTestState *qts = k230_ssi_start_with_flash(&image);
    uint8_t command = FLASH_CMD_JEDEC;
    uint8_t id[3];
    uint8_t actual[ARRAY_SIZE(expected)];
    uint8_t program[4 + ARRAY_SIZE(payload)];
    uint32_t addr = K230_SSI_FLASH_PROGRAM_ADDR;

    flash_read_transaction(qts, &command, 1, id, sizeof(id));
    g_assert_cmphex(id[0], ==, 0xef);
    g_assert_cmphex(id[1], ==, 0x40);
    g_assert_cmphex(id[2], ==, 0x19);
    flash_read(qts, FLASH_CMD_READ, K230_SSI_FLASH_PATTERN_ADDR,
               3, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    flash_write_enable(qts);
    program[0] = FLASH_CMD_PP;
    program[1] = addr >> 16;
    program[2] = addr >> 8;
    program[3] = addr;
    memcpy(program + 4, payload, sizeof(payload));
    flash_write_transaction(qts, program, sizeof(program));
    flash_wait_ready(qts);
    flash_read(qts, FLASH_CMD_READ, addr, 3, actual, sizeof(payload));
    g_assert_cmpmem(actual, sizeof(payload), payload, sizeof(payload));

    qtest_quit(qts);
    k230_ssi_flash_image_clear(&image);
}

static void test_qspi_sdr(void)
{
    static const uint8_t expected[] = { 0xa5, 0x5a, 0x3c, 0xc3 };
    static const uint8_t payload[] = { 0x12, 0x34, 0x56, 0x78 };
    K230SsiFlashImage image;
    QTestState *qts = k230_ssi_start_with_flash(&image);
    uint8_t actual[ARRAY_SIZE(expected)];

    configure_enhanced_transfer(qts, K230_SSI_TMOD_RO,
                                K230_SSI_FRF_QUAD, 0, 8, false,
                                ARRAY_SIZE(actual));
    start_enhanced_transfer(qts, FLASH_CMD_QUAD_OUT,
                            K230_SSI_FLASH_PATTERN_ADDR);
    read_enhanced_result(qts, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    flash_write_enable(qts);
    configure_enhanced_transfer(qts, K230_SSI_TMOD_TO,
                                K230_SSI_FRF_QUAD, 0, 0, false,
                                ARRAY_SIZE(payload));
    start_enhanced_transfer(qts, FLASH_CMD_QUAD_PP,
                            K230_SSI_FLASH_PROGRAM_ADDR);
    for (int i = 0; i < ARRAY_SIZE(payload); i++) {
        k230_ssi_write_frame(qts, K230_SPI0_BASE, payload[i]);
    }
    k230_ssi_wait_mask(qts, K230_SPI0_BASE, K230_SSI_SR,
                       K230_SSI_SR_BUSY, 0);
    k230_ssi_disable(qts, K230_SPI0_BASE);
    flash_wait_ready(qts);

    configure_enhanced_transfer(qts, K230_SSI_TMOD_RO,
                                K230_SSI_FRF_QUAD, 0, 8, false,
                                ARRAY_SIZE(actual));
    start_enhanced_transfer(qts, FLASH_CMD_QUAD_OUT,
                            K230_SSI_FLASH_PROGRAM_ADDR);
    read_enhanced_result(qts, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(payload), payload, sizeof(payload));

    qtest_quit(qts);
    k230_ssi_flash_image_clear(&image);
}

static void test_idma(void)
{
    static const uint8_t expected[] = { 0xa5, 0x5a, 0x3c, 0xc3 };
    K230SsiFlashImage image;
    QTestState *qts = k230_ssi_start_with_flash(&image);
    uint8_t actual[ARRAY_SIZE(expected)];

    qtest_memset(qts, K230_SSI_DMA_ADDR, 0, sizeof(actual));
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_IMR, 0);
    configure_idma(qts, K230_SSI_TMOD_RO, FLASH_CMD_QUAD_OUT,
                   K230_SSI_FLASH_PATTERN_ADDR, K230_SSI_DMA_ADDR,
                   sizeof(actual));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_DONE, ==, K230_SSI_INT_DONE);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_ISR) &
                    K230_SSI_INT_DONE, ==, 0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_IMR,
                    K230_SSI_INT_DONE);
    g_assert_true(k230_ssi_plic_pending(
        qts, k230_ssi_instances[0].first_irq + K230_SSI_IRQ_DONE));
    assert_idma_stopped(qts, sizeof(actual));
    qtest_memread(qts, K230_SSI_DMA_ADDR, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_DONECR, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_DONECR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_DONE, ==, 0);

    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_IMR, 0);
    configure_idma(qts, K230_SSI_TMOD_RO, FLASH_CMD_QUAD_OUT,
                   K230_SSI_FLASH_PATTERN_ADDR, 0x100000000ULL,
                   sizeof(actual));
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_AXIE, ==, K230_SSI_INT_AXIE);
    assert_idma_stopped(qts, 0);
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_IMR,
                    K230_SSI_INT_AXIE);
    g_assert_true(k230_ssi_plic_pending(
        qts, k230_ssi_instances[0].first_irq + K230_SSI_IRQ_AXIE));
    k230_ssi_writel(qts, K230_SPI0_BASE, K230_SSI_AXIECR, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_AXIECR),
                    ==, 1);
    g_assert_cmphex(k230_ssi_readl(qts, K230_SPI0_BASE, K230_SSI_RISR) &
                    K230_SSI_INT_AXIE, ==, 0);

    qtest_quit(qts);
    k230_ssi_flash_image_clear(&image);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-dw-ssi/register-contract", test_register_contract);
    qtest_add_func("/k230-dw-ssi/pio-data-path", test_pio_data_path);
    qtest_add_func("/k230-dw-ssi/interrupt-controller",
                   test_interrupt_controller);
    qtest_add_func("/k230-dw-ssi/plic-routing", test_plic_routing);
    qtest_add_func("/k230-dw-ssi/qspi-config", test_qspi_config);
    qtest_add_func("/k230-dw-ssi/spi-nor", test_spi_nor);
    qtest_add_func("/k230-dw-ssi/qspi-sdr", test_qspi_sdr);
    qtest_add_func("/k230-dw-ssi/idma", test_idma);
    return g_test_run();
}
