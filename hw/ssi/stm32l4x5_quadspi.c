/*
 * STM32L4x5 QuadSPI controller
 *
 * Copyright (c) 2026 EXALT Technologies
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The reference used is the STMicroelectronics RM0351 reference manual for
 * STM32L47xxx, STM32L48xxx, STM32L49xxx and STM32L4Axxx MCUs.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"
#include "hw/ssi/stm32l4x5_quadspi.h"
#include "migration/vmstate.h"

#define QUADSPI_MMIO_SIZE 0x400
#define QUADSPI_FIFO_SIZE 16

#define QUADSPI_CR    0x00
#define QUADSPI_DCR   0x04
#define QUADSPI_SR    0x08
#define QUADSPI_FCR   0x0c
#define QUADSPI_DLR   0x10
#define QUADSPI_CCR   0x14
#define QUADSPI_AR    0x18
#define QUADSPI_ABR   0x1c
#define QUADSPI_DR    0x20
#define QUADSPI_PSMKR 0x24
#define QUADSPI_PSMAR 0x28
#define QUADSPI_PIR   0x2c
#define QUADSPI_LPTR  0x30

#define CR_EN       BIT(0)
#define CR_ABORT    BIT(1)
#define CR_DFM      BIT(6)
#define CR_FTHRES_SHIFT 8
#define CR_FTHRES   MAKE_64BIT_MASK(8, 4)
#define CR_TEIE     BIT(16)
#define CR_TCIE     BIT(17)
#define CR_FTIE     BIT(18)
#define CR_SMIE     BIT(19)
#define CR_TOIE     BIT(20)

#define SR_TEF      BIT(0)
#define SR_TCF      BIT(1)
#define SR_FTF      BIT(2)
#define SR_SMF      BIT(3)
#define SR_TOF      BIT(4)
#define SR_BUSY     BIT(5)
#define SR_FLEVEL   MAKE_64BIT_MASK(8, 5)
#define SR_FLEVEL_SHIFT 8

#define CCR_INSTRUCTION MAKE_64BIT_MASK(0, 8)
#define CCR_IMODE       MAKE_64BIT_MASK(8, 2)
#define CCR_ADMODE      MAKE_64BIT_MASK(10, 2)
#define CCR_ADSIZE      MAKE_64BIT_MASK(12, 2)
#define CCR_ABMODE      MAKE_64BIT_MASK(14, 2)
#define CCR_ABSIZE      MAKE_64BIT_MASK(16, 2)
#define CCR_DCYC        MAKE_64BIT_MASK(18, 5)
#define CCR_DMODE       MAKE_64BIT_MASK(24, 2)
#define CCR_FMODE       MAKE_64BIT_MASK(26, 2)
#define CCR_SIOO        BIT(28)
#define CCR_DDRM        BIT(31)

#define CCR_IMODE_SHIFT 8
#define CCR_ADMODE_SHIFT 10
#define CCR_ADSIZE_SHIFT 12
#define CCR_ABMODE_SHIFT 14
#define CCR_ABSIZE_SHIFT 16
#define CCR_DCYC_SHIFT 18
#define CCR_DMODE_SHIFT 24
#define CCR_FMODE_SHIFT 26

enum QuadSpiFunctionalMode {
    QUADSPI_INDIRECT_WRITE,
    QUADSPI_INDIRECT_READ,
    QUADSPI_AUTO_POLLING,
    QUADSPI_MEMORY_MAPPED,
};

static unsigned int stm32l4x5_quadspi_mode_width(uint32_t mode)
{
    static const unsigned int widths[] = { 0, 1, 2, 4 };

    return widths[mode & 3];
}

static unsigned int stm32l4x5_quadspi_fmode(Stm32l4x5QuadSpiState *s)
{
    return (s->ccr & CCR_FMODE) >> CCR_FMODE_SHIFT;
}

static void stm32l4x5_quadspi_update_irq(Stm32l4x5QuadSpiState *s)
{
    bool level = ((s->sr & SR_TEF) && (s->cr & CR_TEIE)) ||
                 ((s->sr & SR_TCF) && (s->cr & CR_TCIE)) ||
                 ((s->sr & SR_FTF) && (s->cr & CR_FTIE)) ||
                 ((s->sr & SR_SMF) && (s->cr & CR_SMIE)) ||
                 ((s->sr & SR_TOF) && (s->cr & CR_TOIE));

    qemu_set_irq(s->irq, level);
}

static void stm32l4x5_quadspi_update_fifo(Stm32l4x5QuadSpiState *s)
{
    unsigned int level = fifo8_num_used(&s->fifo);
    unsigned int threshold = ((s->cr & CR_FTHRES) >> CR_FTHRES_SHIFT) + 1;
    unsigned int mode = stm32l4x5_quadspi_fmode(s);

    s->sr &= ~(SR_FLEVEL | SR_FTF | SR_BUSY);
    s->sr |= level << SR_FLEVEL_SHIFT;
    if (s->transaction_active ||
        (mode == QUADSPI_INDIRECT_READ && level)) {
        s->sr |= SR_BUSY;
    }
    if (s->cr & CR_EN) {
        if (mode == QUADSPI_INDIRECT_READ &&
            (level >= threshold ||
             (!s->remaining && !s->indefinite && level))) {
            s->sr |= SR_FTF;
        } else if (mode == QUADSPI_INDIRECT_WRITE &&
                   fifo8_num_free(&s->fifo) >= threshold) {
            s->sr |= SR_FTF;
        }
    }
    stm32l4x5_quadspi_update_irq(s);
}

static void stm32l4x5_quadspi_select(Stm32l4x5QuadSpiState *s)
{
    if (!s->transaction_active) {
        qemu_set_irq(s->cs, 0);
        s->transaction_active = true;
        s->sr |= SR_BUSY;
    }
}

static void stm32l4x5_quadspi_deselect(Stm32l4x5QuadSpiState *s)
{
    if (s->transaction_active) {
        qemu_set_irq(s->cs, 1);
        s->transaction_active = false;
    }
    s->remaining = 0;
    s->indefinite = false;
    s->write_pending = false;
}

static void stm32l4x5_quadspi_transfer_value(Stm32l4x5QuadSpiState *s,
                                              uint32_t value,
                                              unsigned int count)
{
    for (int i = count - 1; i >= 0; i--) {
        ssi_transfer(s->ssi, extract32(value, i * 8, 8));
    }
}

static void stm32l4x5_quadspi_send_prefix(Stm32l4x5QuadSpiState *s)
{
    unsigned int imode = (s->ccr & CCR_IMODE) >> CCR_IMODE_SHIFT;
    unsigned int admode = (s->ccr & CCR_ADMODE) >> CCR_ADMODE_SHIFT;
    unsigned int abmode = (s->ccr & CCR_ABMODE) >> CCR_ABMODE_SHIFT;
    unsigned int dmode = (s->ccr & CCR_DMODE) >> CCR_DMODE_SHIFT;
    unsigned int dummy_cycles = (s->ccr & CCR_DCYC) >> CCR_DCYC_SHIFT;
    unsigned int dummy_width;
    unsigned int dummy_bytes;

    stm32l4x5_quadspi_select(s);

    if (imode) {
        ssi_transfer(s->ssi, s->ccr & CCR_INSTRUCTION);
    }
    if (admode) {
        unsigned int address_bytes =
            ((s->ccr & CCR_ADSIZE) >> CCR_ADSIZE_SHIFT) + 1;

        stm32l4x5_quadspi_transfer_value(s, s->ar, address_bytes);
    }
    if (abmode) {
        unsigned int alternate_bytes =
            ((s->ccr & CCR_ABSIZE) >> CCR_ABSIZE_SHIFT) + 1;

        stm32l4x5_quadspi_transfer_value(s, s->abr, alternate_bytes);
    }

    dummy_width = stm32l4x5_quadspi_mode_width(admode ? admode : dmode);
    dummy_bytes = DIV_ROUND_UP(dummy_cycles * dummy_width, 8);
    for (unsigned int i = 0; i < dummy_bytes; i++) {
        ssi_transfer(s->ssi, 0);
    }
}

static void stm32l4x5_quadspi_complete(Stm32l4x5QuadSpiState *s)
{
    stm32l4x5_quadspi_deselect(s);
    s->rx_stalled = false;
    if (stm32l4x5_quadspi_fmode(s) == QUADSPI_INDIRECT_WRITE) {
        fifo8_reset(&s->fifo);
    }
    s->sr |= SR_TCF;
    stm32l4x5_quadspi_update_fifo(s);
}

static bool stm32l4x5_quadspi_prepare_data(Stm32l4x5QuadSpiState *s)
{
    unsigned int fsize = extract32(s->dcr, 16, 5);
    uint64_t flash_size = 1ULL << (fsize + 1);
    uint64_t address = (s->ccr & CCR_ADMODE) ? s->ar : 0;
    bool has_data = s->ccr & CCR_DMODE;

    fifo8_reset(&s->fifo);
    s->rx_stalled = false;
    s->write_pending = false;
    s->indefinite = has_data && s->dlr == UINT32_MAX && fsize == 31;
    s->remaining = has_data ? (uint64_t)s->dlr + 1 : 0;
    s->sr &= ~SR_TCF;
    if ((s->ccr & CCR_ADMODE) &&
        (address >= flash_size ||
         (has_data && s->dlr != UINT32_MAX &&
          s->remaining > flash_size - address))) {
        s->remaining = 0;
        s->indefinite = false;
        s->sr |= SR_TEF;
        stm32l4x5_quadspi_update_fifo(s);
        return false;
    }
    if (has_data && s->dlr == UINT32_MAX) {
        s->remaining = s->indefinite ? 0 : flash_size - address;
    }
    stm32l4x5_quadspi_update_fifo(s);
    return true;
}

static bool stm32l4x5_quadspi_indirect(Stm32l4x5QuadSpiState *s)
{
    if (stm32l4x5_quadspi_fmode(s) >= QUADSPI_AUTO_POLLING ||
        (s->ccr & (CCR_SIOO | CCR_DDRM)) || (s->cr & CR_DFM)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: polling, mapped, SIOO, DDR, or dual flash mode\n",
                      TYPE_STM32L4X5_QUADSPI);
        return false;
    }
    return true;
}

static void stm32l4x5_quadspi_fill_fifo(Stm32l4x5QuadSpiState *s)
{
    if (!(s->cr & CR_EN) || !s->transaction_active) {
        return;
    }
    /* After a full FIFO, reception resumes when FLEVEL is at most 11. */
    if (s->rx_stalled && fifo8_num_used(&s->fifo) > 11) {
        return;
    }
    s->rx_stalled = false;
    while ((s->remaining || s->indefinite) && !fifo8_is_full(&s->fifo)) {
        fifo8_push(&s->fifo, ssi_transfer(s->ssi, 0));
        if (!s->indefinite) {
            s->remaining--;
        }
    }
    if (!s->remaining && !s->indefinite) {
        stm32l4x5_quadspi_complete(s);
    } else {
        s->rx_stalled = true;
        stm32l4x5_quadspi_update_fifo(s);
    }
}

static void stm32l4x5_quadspi_start_read(Stm32l4x5QuadSpiState *s)
{
    if (!stm32l4x5_quadspi_prepare_data(s)) {
        return;
    }
    stm32l4x5_quadspi_send_prefix(s);
    stm32l4x5_quadspi_fill_fifo(s);
}

static void stm32l4x5_quadspi_execute_command(Stm32l4x5QuadSpiState *s)
{
    if (!stm32l4x5_quadspi_prepare_data(s)) {
        return;
    }
    stm32l4x5_quadspi_send_prefix(s);
    stm32l4x5_quadspi_complete(s);
}

static uint64_t stm32l4x5_quadspi_read_register(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    Stm32l4x5QuadSpiState *s = opaque;
    uint32_t value = 0;

    switch (addr) {
    case QUADSPI_CR:
        return s->cr;
    case QUADSPI_DCR:
        return s->dcr;
    case QUADSPI_SR:
        return s->sr;
    case QUADSPI_FCR:
        return 0;
    case QUADSPI_DLR:
        return s->dlr;
    case QUADSPI_CCR:
        return s->ccr;
    case QUADSPI_AR:
        return s->ar;
    case QUADSPI_ABR:
        return s->abr;
    case QUADSPI_DR:
        if (!(s->cr & CR_EN) ||
            stm32l4x5_quadspi_fmode(s) != QUADSPI_INDIRECT_READ) {
            return 0;
        }
        for (unsigned int i = 0; i < size && !fifo8_is_empty(&s->fifo); i++) {
            value |= (uint32_t)fifo8_pop(&s->fifo) << (i * 8);
        }
        s->dr = value;
        stm32l4x5_quadspi_fill_fifo(s);
        stm32l4x5_quadspi_update_fifo(s);
        return value;
    case QUADSPI_PSMKR:
        return s->psmkr;
    case QUADSPI_PSMAR:
        return s->psmar;
    case QUADSPI_PIR:
        return s->pir;
    case QUADSPI_LPTR:
        return s->lptr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      TYPE_STM32L4X5_QUADSPI, addr);
        return 0;
    }
}

static void stm32l4x5_quadspi_write_data(Stm32l4x5QuadSpiState *s,
                                          uint32_t value,
                                          unsigned int size)
{
    if (!(s->cr & CR_EN) ||
        stm32l4x5_quadspi_fmode(s) != QUADSPI_INDIRECT_WRITE ||
        (!s->write_pending && !s->remaining && !s->indefinite)) {
        return;
    }
    if (s->write_pending) {
        if (!stm32l4x5_quadspi_indirect(s) ||
            !stm32l4x5_quadspi_prepare_data(s)) {
            s->write_pending = false;
            return;
        }
    }
    if (fifo8_num_free(&s->fifo) < size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to full FIFO\n",
                      TYPE_STM32L4X5_QUADSPI);
        return;
    }
    for (unsigned int i = 0; i < size; i++) {
        fifo8_push(&s->fifo, extract32(value, i * 8, 8));
    }
    s->dr = value;
    if (!s->transaction_active) {
        stm32l4x5_quadspi_send_prefix(s);
    }

    while ((s->remaining || s->indefinite) && !fifo8_is_empty(&s->fifo)) {
        ssi_transfer(s->ssi, fifo8_pop(&s->fifo));
        if (!s->indefinite) {
            s->remaining--;
        }
    }

    if (!s->remaining && !s->indefinite) {
        stm32l4x5_quadspi_complete(s);
    } else {
        stm32l4x5_quadspi_update_fifo(s);
    }
}

static void stm32l4x5_quadspi_abort(Stm32l4x5QuadSpiState *s)
{
    stm32l4x5_quadspi_deselect(s);
    fifo8_reset(&s->fifo);
    s->rx_stalled = false;
    s->cr &= ~CR_ABORT;
    s->sr |= SR_TCF;
    stm32l4x5_quadspi_update_fifo(s);
}

static void stm32l4x5_quadspi_write_register(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned int size)
{
    Stm32l4x5QuadSpiState *s = opaque;

    switch (addr) {
    case QUADSPI_CR:
        if (s->sr & SR_BUSY) {
            uint32_t mutable = CR_EN | CR_ABORT | CR_TEIE | CR_TCIE |
                               CR_FTIE | CR_SMIE | CR_TOIE;
            value = (s->cr & ~mutable) | (value & mutable);
        }
        s->cr = value;
        if (s->cr & CR_ABORT) {
            stm32l4x5_quadspi_abort(s);
        } else {
            if (!(s->cr & CR_EN)) {
                /* Disabling is not a reset or a transfer-complete event. */
                stm32l4x5_quadspi_deselect(s);
                fifo8_reset(&s->fifo);
                s->rx_stalled = false;
            }
            stm32l4x5_quadspi_update_fifo(s);
        }
        return;
    case QUADSPI_DCR:
        s->dcr = value;
        return;
    case QUADSPI_SR:
        return;
    case QUADSPI_FCR:
        s->sr &= ~(value & (SR_TEF | SR_TCF | SR_SMF | SR_TOF));
        stm32l4x5_quadspi_update_irq(s);
        return;
    case QUADSPI_DLR:
        s->dlr = value;
        return;
    case QUADSPI_CCR:
        s->ccr = value;
        s->write_pending = false;
        if (!(s->cr & CR_EN) || !stm32l4x5_quadspi_indirect(s)) {
            return;
        }
        if (((s->ccr & CCR_DMODE) == 0) &&
            ((s->ccr & CCR_ADMODE) == 0) &&
            ((s->ccr & CCR_IMODE) != 0)) {
            stm32l4x5_quadspi_execute_command(s);
        } else if ((s->ccr & CCR_DMODE) &&
                   stm32l4x5_quadspi_fmode(s) == QUADSPI_INDIRECT_WRITE) {
            s->write_pending = true;
            stm32l4x5_quadspi_update_fifo(s);
        } else if ((s->ccr & CCR_DMODE) &&
                   !(s->ccr & CCR_ADMODE) &&
                   stm32l4x5_quadspi_fmode(s) == QUADSPI_INDIRECT_READ) {
            stm32l4x5_quadspi_start_read(s);
        }
        return;
    case QUADSPI_AR:
        s->ar = value;
        if (!(s->cr & CR_EN) || !stm32l4x5_quadspi_indirect(s)) {
            return;
        }
        if ((s->ccr & CCR_DMODE) && (s->ccr & CCR_ADMODE) &&
            stm32l4x5_quadspi_fmode(s) == QUADSPI_INDIRECT_READ) {
            stm32l4x5_quadspi_start_read(s);
        } else if (((s->ccr & CCR_DMODE) == 0) &&
                   ((s->ccr & CCR_ADMODE) != 0)) {
            stm32l4x5_quadspi_execute_command(s);
        }
        return;
    case QUADSPI_ABR:
        s->abr = value;
        return;
    case QUADSPI_DR:
        stm32l4x5_quadspi_write_data(s, value, size);
        return;
    case QUADSPI_PSMKR:
        s->psmkr = value;
        return;
    case QUADSPI_PSMAR:
        s->psmar = value;
        return;
    case QUADSPI_PIR:
        s->pir = value;
        return;
    case QUADSPI_LPTR:
        s->lptr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      TYPE_STM32L4X5_QUADSPI, addr);
    }
}

static uint64_t stm32l4x5_quadspi_read(void *opaque, hwaddr addr,
                                      unsigned int size)
{
    uint64_t value;

    if ((addr & ~3ULL) == QUADSPI_DR) {
        /* DR access size determines the number of FIFO bytes consumed. */
        return stm32l4x5_quadspi_read_register(opaque, addr, size);
    }
    value = stm32l4x5_quadspi_read_register(opaque, addr & ~3ULL, 4);
    return extract32(value, (addr & 3) * 8, size * 8);
}

static void stm32l4x5_quadspi_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned int size)
{
    Stm32l4x5QuadSpiState *s = opaque;
    unsigned int shift = (addr & 3) * 8;
    hwaddr reg = addr & ~3ULL;

    if ((s->sr & SR_BUSY) && reg != QUADSPI_CR &&
        reg != QUADSPI_FCR && reg != QUADSPI_SR && reg != QUADSPI_DR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: configuration write while busy\n",
                      TYPE_STM32L4X5_QUADSPI);
        return;
    }
    if (reg == QUADSPI_FCR) {
        value <<= shift;
    } else if (reg != QUADSPI_DR && size != 4) {
        uint32_t mask = MAKE_64BIT_MASK(shift, size * 8);
        uint32_t old = stm32l4x5_quadspi_read_register(s, reg, 4);

        value = (old & ~mask) | ((value << shift) & mask);
    }
    stm32l4x5_quadspi_write_register(s, reg, value, size);
}

static bool stm32l4x5_quadspi_accepts(void *opaque, hwaddr addr,
                                   unsigned int size, bool is_write,
                                   MemTxAttrs attrs)
{
    return (size == 1 || size == 2 || size == 4) &&
           ((addr & ~3ULL) != QUADSPI_DR || addr == QUADSPI_DR);
}

static const MemoryRegionOps stm32l4x5_quadspi_ops = {
    .read = stm32l4x5_quadspi_read,
    .write = stm32l4x5_quadspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
    .valid.accepts = stm32l4x5_quadspi_accepts,
};

static void stm32l4x5_quadspi_hold_reset(Object *obj, ResetType type)
{
    Stm32l4x5QuadSpiState *s = STM32L4X5_QUADSPI(obj);

    s->cr = 0;
    s->dcr = 0;
    s->sr = 0;
    s->dlr = 0;
    s->ccr = 0;
    s->ar = 0;
    s->abr = 0;
    s->dr = 0;
    s->psmkr = 0;
    s->psmar = 0;
    s->pir = 0;
    s->lptr = 0;
    s->remaining = 0;
    s->transaction_active = false;
    s->rx_stalled = false;
    s->indefinite = false;
    s->write_pending = false;
    fifo8_reset(&s->fifo);
    qemu_set_irq(s->cs, 1);
    qemu_irq_lower(s->irq);
}

static void stm32l4x5_quadspi_init(Object *obj)
{
    Stm32l4x5QuadSpiState *s = STM32L4X5_QUADSPI(obj);
    DeviceState *dev = DEVICE(obj);

    fifo8_create(&s->fifo, QUADSPI_FIFO_SIZE);
    memory_region_init_io(&s->mmio, obj, &stm32l4x5_quadspi_ops, s,
                          TYPE_STM32L4X5_QUADSPI, QUADSPI_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->cs, "cs", 1);
    s->ssi = ssi_create_bus(dev, "ssi");
    s->clk = qdev_init_clock_in(dev, "clk", NULL, s, 0);
}

static void stm32l4x5_quadspi_finalize(Object *obj)
{
    Stm32l4x5QuadSpiState *s = STM32L4X5_QUADSPI(obj);

    fifo8_destroy(&s->fifo);
}

static void stm32l4x5_quadspi_realize(DeviceState *dev, Error **errp)
{
    Stm32l4x5QuadSpiState *s = STM32L4X5_QUADSPI(dev);

    if (!clock_has_source(s->clk)) {
        error_setg(errp, "%s: clk input must be connected",
                   TYPE_STM32L4X5_QUADSPI);
    }
}

static int stm32l4x5_quadspi_post_load(void *opaque, int version_id)
{
    Stm32l4x5QuadSpiState *s = opaque;

    if (s->fifo.head >= QUADSPI_FIFO_SIZE ||
        fifo8_num_used(&s->fifo) > QUADSPI_FIFO_SIZE) {
        return -EINVAL;
    }
    if ((s->transaction_active && !(s->cr & CR_EN)) ||
        (s->indefinite && s->remaining)) {
        return -EINVAL;
    }
    qemu_set_irq(s->cs, !s->transaction_active);
    stm32l4x5_quadspi_update_fifo(s);
    return 0;
}

static const VMStateDescription vmstate_stm32l4x5_quadspi = {
    .name = TYPE_STM32L4X5_QUADSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = stm32l4x5_quadspi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(dcr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(sr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(dlr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(ccr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(ar, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(abr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(dr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(psmkr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(psmar, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(pir, Stm32l4x5QuadSpiState),
        VMSTATE_UINT32(lptr, Stm32l4x5QuadSpiState),
        VMSTATE_UINT64(remaining, Stm32l4x5QuadSpiState),
        VMSTATE_BOOL(transaction_active, Stm32l4x5QuadSpiState),
        VMSTATE_FIFO8(fifo, Stm32l4x5QuadSpiState),
        VMSTATE_BOOL(rx_stalled, Stm32l4x5QuadSpiState),
        VMSTATE_BOOL(indefinite, Stm32l4x5QuadSpiState),
        VMSTATE_BOOL(write_pending, Stm32l4x5QuadSpiState),
        VMSTATE_CLOCK(clk, Stm32l4x5QuadSpiState),
        VMSTATE_END_OF_LIST()
    },
};

static void stm32l4x5_quadspi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = stm32l4x5_quadspi_realize;
    dc->vmsd = &vmstate_stm32l4x5_quadspi;
    rc->phases.hold = stm32l4x5_quadspi_hold_reset;
}

static const TypeInfo stm32l4x5_quadspi_info = {
    .name = TYPE_STM32L4X5_QUADSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Stm32l4x5QuadSpiState),
    .instance_init = stm32l4x5_quadspi_init,
    .instance_finalize = stm32l4x5_quadspi_finalize,
    .class_init = stm32l4x5_quadspi_class_init,
};

static void stm32l4x5_quadspi_register_types(void)
{
    type_register_static(&stm32l4x5_quadspi_info);
}

type_init(stm32l4x5_quadspi_register_types)
