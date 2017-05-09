/*
 * SPI controller model of Microsemi SmartFusion2.
 *
 * Copyright (C) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
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

#include "hw/ssi/msf2-spi.h"

#ifndef MSF2_SPI_ERR_DEBUG
#define MSF2_SPI_ERR_DEBUG   0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (MSF2_SPI_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void txfifo_reset(MSF2SpiState *s)
{
    fifo32_reset(&s->tx_fifo);

    s->regs[R_SPI_STATUS] &= ~S_TXFIFOFUL;
    s->regs[R_SPI_STATUS] |= S_TXFIFOEMP;
}

static void rxfifo_reset(MSF2SpiState *s)
{
    fifo32_reset(&s->rx_fifo);

    s->regs[R_SPI_STATUS] &= ~S_RXFIFOFUL;
    s->regs[R_SPI_STATUS] |= S_RXFIFOEMP;
}

static void set_fifodepth(MSF2SpiState *s)
{
    int size = s->regs[R_SPI_DFSIZE] & FRAMESZ_MASK;

    if (0 <= size && size <= 8) {
        s->fifo_depth = 32;
    }
    if (9 <= size && size <= 16) {
        s->fifo_depth = 16;
    }
    if (17 <= size && size <= 32) {
        s->fifo_depth = 8;
    }
}

static void msf2_spi_do_reset(MSF2SpiState *s)
{
    memset(s->regs, 0, sizeof s->regs);
    s->regs[R_SPI_CONTROL] = 0x80000102;
    s->regs[R_SPI_DFSIZE] = 0x4;
    s->regs[R_SPI_STATUS] = 0x2440;
    s->regs[R_SPI_CLKGEN] = 0x7;
    s->regs[R_SPI_STAT8] = 0x7;
    s->regs[R_SPI_RIS] = 0x0;

    s->fifo_depth = 4;
    s->frame_count = 1;
    s->enabled = false;

    rxfifo_reset(s);
    txfifo_reset(s);
}

static void update_mis(MSF2SpiState *s)
{
    uint32_t reg = s->regs[R_SPI_CONTROL];
    uint32_t tmp;

    /*
     * form the Control register interrupt enable bits
     * same as RIS, MIS and Interrupt clear registers for simplicity
     */
    tmp = ((reg & C_INTRXOVRFLO) >> 4) | ((reg & C_INTRXDATA) >> 3) |
           ((reg & C_INTTXDATA) >> 5);
    s->regs[R_SPI_MIS] |= tmp & s->regs[R_SPI_RIS];
}

static void spi_update_irq(MSF2SpiState *s)
{
    int irq;

    update_mis(s);
    irq = !!(s->regs[R_SPI_MIS]);

    qemu_set_irq(s->irq, irq);
}

static void msf2_spi_reset(DeviceState *d)
{
    msf2_spi_do_reset(MSF2_SPI(d));
}

static uint64_t
spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    MSF2SpiState *s = opaque;
    uint32_t ret = 0;

    addr >>= 2;
    switch (addr) {
    case R_SPI_RX:
        s->regs[R_SPI_STATUS] &= ~S_RXFIFOFUL;
        s->regs[R_SPI_STATUS] &= ~RXCHOVRF;
        ret = fifo32_pop(&s->rx_fifo);
        if (fifo32_is_empty(&s->rx_fifo)) {
            s->regs[R_SPI_STATUS] |= S_RXFIFOEMP;
        }
        break;

    case R_SPI_MIS:
        update_mis(s);
        ret = s->regs[R_SPI_MIS];
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            ret = s->regs[addr];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                         "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                         addr * 4);
        }
        break;
    }

    DB_PRINT("addr=0x%" HWADDR_PRIx " = 0x%" PRIx32 "\n", addr * 4, ret);
    spi_update_irq(s);
    return ret;
}

static void assert_cs(MSF2SpiState *s)
{
    qemu_set_irq(s->cs_line, 0);
}

static void deassert_cs(MSF2SpiState *s)
{
    qemu_set_irq(s->cs_line, 1);
}

static void spi_flush_txfifo(MSF2SpiState *s)
{
    uint32_t tx;
    uint32_t rx;
    bool sps = !!(s->regs[R_SPI_CONTROL] & C_SPS);

    /*
     * Chip Select(CS) is automatically controlled by this controller.
     * If SPS bit is set in Control register then CS is asserted
     * until all the frames set in frame count of Control register are
     * transferred. If SPS is not set then CS pulses between frames.
     * Note that Slave Select register specifies which of the CS line
     * has to be controlled automatically by controller. Bits SS[7:1] are for
     * masters in FPGA fabric since we model only Microcontroller subsystem
     * of Smartfusion2 we control only one CS(SS[0]) line.
     */
    while (!fifo32_is_empty(&s->tx_fifo) && s->frame_count) {
        assert_cs(s);

        s->regs[R_SPI_STATUS] &= ~TXDONE;
        s->regs[R_SPI_STATUS] &= ~RXRDY;

        tx = fifo32_pop(&s->tx_fifo);
        DB_PRINT("data tx:0x%" PRIx32 "\n", tx);
        rx = ssi_transfer(s->spi, tx);
        DB_PRINT("data rx:0x%" PRIx32 "\n", rx);

        if (fifo32_num_used(&s->rx_fifo) == s->fifo_depth) {
            s->regs[R_SPI_STATUS] |= RXCHOVRF;
            s->regs[R_SPI_RIS] |= RXCHOVRF;
        } else {
            fifo32_push(&s->rx_fifo, rx);
            s->regs[R_SPI_STATUS] &= ~S_RXFIFOEMP;
            if (fifo32_num_used(&s->rx_fifo) == (s->fifo_depth - 1)) {
                s->regs[R_SPI_STATUS] |= S_RXFIFOFULNXT;
            }
            if (fifo32_num_used(&s->rx_fifo) == s->fifo_depth) {
                s->regs[R_SPI_STATUS] |= S_RXFIFOFUL;
            }
        }
        s->frame_count--;
        if (!sps) {
            deassert_cs(s);
            assert_cs(s);
        }
    }

    if (!sps) {
        deassert_cs(s);
    }

    if (!s->frame_count) {
        s->frame_count = (s->regs[R_SPI_CONTROL] & FMCOUNT_MASK) >>
                            FMCOUNT_SHIFT;
        if (sps) {
            deassert_cs(s);
        }
        s->regs[R_SPI_RIS] |= TXDONE;
        s->regs[R_SPI_RIS] |= RXRDY;
        s->regs[R_SPI_STATUS] |= TXDONE;
        s->regs[R_SPI_STATUS] |= RXRDY;
   }
}

static void spi_write(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    MSF2SpiState *s = opaque;
    uint32_t value = val64;

    DB_PRINT("addr=0x%" HWADDR_PRIx " =0x%" PRIx32 "\n", addr, value);
    addr >>= 2;

    switch (addr) {
    case R_SPI_TX:
        /* adding to already full FIFO */
        if (fifo32_num_used(&s->tx_fifo) == s->fifo_depth) {
            break;
        }
        s->regs[R_SPI_STATUS] &= ~S_TXFIFOEMP;
        fifo32_push(&s->tx_fifo, value);
        if (fifo32_num_used(&s->tx_fifo) == (s->fifo_depth - 1)) {
            s->regs[R_SPI_STATUS] |= S_TXFIFOFULNXT;
        }
        if (fifo32_num_used(&s->tx_fifo) == s->fifo_depth) {
            s->regs[R_SPI_STATUS] |= S_TXFIFOFUL;
        }
        if (s->enabled) {
            spi_flush_txfifo(s);
        }
        break;

    case R_SPI_CONTROL:
        s->regs[R_SPI_CONTROL] = value;
        if (value & C_BIGFIFO) {
            set_fifodepth(s);
        } else {
            s->fifo_depth = 4;
        }
        if (value & C_ENABLE) {
            s->enabled = true;
        } else {
            s->enabled = false;
        }
        s->frame_count = (value & FMCOUNT_MASK) >> FMCOUNT_SHIFT;
        if (value & C_RESET) {
            msf2_spi_do_reset(s);
        }
        break;

    case R_SPI_DFSIZE:
        if (s->enabled) {
            break;
        }
        s->regs[R_SPI_DFSIZE] = value;
        break;

    case R_SPI_INTCLR:
        s->regs[R_SPI_INTCLR] = value;
        if (value & TXDONE) {
            s->regs[R_SPI_RIS] &= ~TXDONE;
        }
        if (value & RXRDY) {
            s->regs[R_SPI_RIS] &= ~RXRDY;
        }
        if (value & RXCHOVRF) {
            s->regs[R_SPI_RIS] &= ~RXCHOVRF;
        }
        break;

    case R_SPI_MIS:
    case R_SPI_STATUS:
    case R_SPI_RIS:
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            s->regs[addr] = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                         "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                         addr * 4);
        }
        break;
    }

    spi_update_irq(s);
}

static const MemoryRegionOps spi_ops = {
    .read = spi_read,
    .write = spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void msf2_spi_realize(DeviceState *dev, Error **errp)
{
    MSF2SpiState *s = MSF2_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DB_PRINT("\n");

    s->spi = ssi_create_bus(dev, "spi0");

    sysbus_init_irq(sbd, &s->irq);
    ssi_auto_connect_slaves(dev, &s->cs_line, s->spi);
    sysbus_init_irq(sbd, &s->cs_line);

    memory_region_init_io(&s->mmio, OBJECT(s), &spi_ops, s,
                          TYPE_MSF2_SPI, R_SPI_MAX * 4);
    sysbus_init_mmio(sbd, &s->mmio);

    fifo32_create(&s->tx_fifo, FIFO_CAPACITY);
    fifo32_create(&s->rx_fifo, FIFO_CAPACITY);
}

static const VMStateDescription vmstate_msf2_spi = {
    .name = TYPE_MSF2_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO32(tx_fifo, MSF2SpiState),
        VMSTATE_FIFO32(rx_fifo, MSF2SpiState),
        VMSTATE_UINT32_ARRAY(regs, MSF2SpiState, R_SPI_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void msf2_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = msf2_spi_realize;
    dc->reset = msf2_spi_reset;
    dc->vmsd = &vmstate_msf2_spi;
}

static const TypeInfo msf2_spi_info = {
    .name           = TYPE_MSF2_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(MSF2SpiState),
    .class_init     = msf2_spi_class_init,
};

static void msf2_spi_register_types(void)
{
    type_register_static(&msf2_spi_info);
}

type_init(msf2_spi_register_types)
