/*
 * QTest testcase for PowerNV 10 Seeprom Communications
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <unistd.h>
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "hw/ssi/pnv_spi_regs.h"

#define P10_XSCOM_BASE          0x000603fc00000000ull
#define SPIC2_XSCOM_BASE        0xc0040

/* To transmit READ opcode and address */
#define READ_OP_TDR_DATA        0x0300010000000000
/*
 * N1 shift - tx 4 bytes (transmit opcode and address)
 * N2 shift - tx and rx 8 bytes.
 */
#define READ_OP_COUNTER_CONFIG  0x2040000000002b00
/* SEQ_OP_SELECT_RESPONDER - N1 Shift - N2 Shift * 5 - SEQ_OP_STOP */
#define READ_OP_SEQUENCER       0x1130404040404010

/* To transmit WREN(Set Write Enable Latch in status0 register) opcode */
#define WRITE_OP_WREN           0x0600000000000000
/* To transmit WRITE opcode, address and data */
#define WRITE_OP_TDR_DATA       0x0300010012345678
/* N1 shift - tx 8 bytes (transmit opcode, address and data) */
#define WRITE_OP_COUNTER_CONFIG 0x4000000000002000
/* SEQ_OP_SELECT_RESPONDER - N1 Shift - SEQ_OP_STOP */
#define WRITE_OP_SEQUENCER      0x1130100000000000

static uint64_t pnv_xscom_addr(uint32_t pcba)
{
    return P10_XSCOM_BASE | ((uint64_t) pcba << 3);
}

static uint64_t pnv_spi_seeprom_xscom_addr(uint32_t reg)
{
    return pnv_xscom_addr(SPIC2_XSCOM_BASE + reg);
}

static void pnv_spi_controller_xscom_write(QTestState *qts, uint32_t reg,
                uint64_t val)
{
    qtest_writeq(qts, pnv_spi_seeprom_xscom_addr(reg), val);
}

static uint64_t pnv_spi_controller_xscom_read(QTestState *qts, uint32_t reg)
{
    return qtest_readq(qts, pnv_spi_seeprom_xscom_addr(reg));
}

static void spi_seeprom_transaction(QTestState *qts)
{
    /* SPI transactions to SEEPROM to read from SEEPROM image */
    pnv_spi_controller_xscom_write(qts, COUNTER_CONFIG_REG,
                                    READ_OP_COUNTER_CONFIG);
    pnv_spi_controller_xscom_write(qts, SEQUENCER_OPERATION_REG,
                                    READ_OP_SEQUENCER);
    pnv_spi_controller_xscom_write(qts, TRANSMIT_DATA_REG, READ_OP_TDR_DATA);
    pnv_spi_controller_xscom_write(qts, TRANSMIT_DATA_REG, 0);
    /* Read 5*8 bytes from SEEPROM at 0x100 */
    uint64_t rdr_val = pnv_spi_controller_xscom_read(qts, RECEIVE_DATA_REG);
    printf("RDR READ = 0x%lx\n", rdr_val);
    rdr_val = pnv_spi_controller_xscom_read(qts, RECEIVE_DATA_REG);
    rdr_val = pnv_spi_controller_xscom_read(qts, RECEIVE_DATA_REG);
    rdr_val = pnv_spi_controller_xscom_read(qts, RECEIVE_DATA_REG);
    rdr_val = pnv_spi_controller_xscom_read(qts, RECEIVE_DATA_REG);
    printf("RDR READ = 0x%lx\n", rdr_val);

    /* SPI transactions to SEEPROM to write to SEEPROM image */
    pnv_spi_controller_xscom_write(qts, COUNTER_CONFIG_REG,
                                    WRITE_OP_COUNTER_CONFIG);
    /* Set Write Enable Latch bit of status0 register */
    pnv_spi_controller_xscom_write(qts, SEQUENCER_OPERATION_REG,
                                    WRITE_OP_SEQUENCER);
    pnv_spi_controller_xscom_write(qts, TRANSMIT_DATA_REG, WRITE_OP_WREN);
    /* write 8 bytes to SEEPROM at 0x100 */
    pnv_spi_controller_xscom_write(qts, SEQUENCER_OPERATION_REG,
                                    WRITE_OP_SEQUENCER);
    pnv_spi_controller_xscom_write(qts, TRANSMIT_DATA_REG, WRITE_OP_TDR_DATA);
}

/* Find complete path of in_file in the current working directory */
static void find_file(const char *in_file, char *in_path)
{
    g_autofree char *cwd = g_get_current_dir();
    char *filepath = g_build_filename(cwd, in_file, NULL);
    if (!access(filepath, F_OK)) {
        strcpy(in_path, filepath);
    } else {
        strcpy(in_path, "");
        printf("File %s not found within %s\n", in_file, cwd);
    }
}

static void test_spi_seeprom(void)
{
    QTestState *qts = NULL;
    char seepromfile[500];
    find_file("sbe_measurement_seeprom.bin.ecc", seepromfile);
    if (strcmp(seepromfile, "")) {
        printf("Starting QEMU with seeprom file.\n");
        qts = qtest_initf("-m 2G -machine powernv10 -smp 2,cores=2,"
                          "threads=1 -accel tcg,thread=single -nographic "
                          "-blockdev node-name=pib_spic2,driver=file,"
			  "filename=sbe_measurement_seeprom.bin.ecc "
			  "-device 25csm04,bus=pnv-spi-bus.2,cs=0,"
			  "drive=pib_spic2");
    } else {
        printf("Starting QEMU without seeprom file.\n");
        qts = qtest_initf("-m 2G -machine powernv10 -smp 2,cores=2,"
                          "threads=1 -accel tcg,thread=single -nographic"
			  " -device 25csm04,bus=pnv-spi-bus.2,cs=0");
    }
    spi_seeprom_transaction(qts);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("spi_seeprom", test_spi_seeprom);
    return g_test_run();
}
