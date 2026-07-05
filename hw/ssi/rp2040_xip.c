/*
 * RP2040 XIP/SSI flash controller emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "elf.h"
#include "exec/memattrs.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/loader.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/ssi/rp2040_xip.h"
#include "qemu/log.h"
#include "system/address-spaces.h"
#include "trace.h"

#define RP2040_XIP_CTRL_EN           0x1
#define RP2040_XIP_CTRL_ERR_BADWRITE 0x2
#define RP2040_XIP_STAT_FLUSH_READY  0x1
#define RP2040_XIP_STAT_FIFO_EMPTY   0x2
#define RP2040_XIP_STAT_FIFO_FULL    0x4
#define RP2040_XIP_FLASH_BASE        0x10000000

#define RP2040_XIP_CTRL              0x00
#define RP2040_XIP_FLUSH             0x04
#define RP2040_XIP_STAT              0x08
#define RP2040_XIP_CTR_HIT           0x0c
#define RP2040_XIP_CTR_ACC           0x10
#define RP2040_XIP_STREAM_ADDR       0x14
#define RP2040_XIP_STREAM_CTR        0x18
#define RP2040_XIP_STREAM_FIFO       0x1c
#define RP2040_XIP_STREAM_CTR_MASK   0x003fffff

#define RP2040_SSI_CTRLR0     0x00
#define RP2040_SSI_CTRLR1     0x04
#define RP2040_SSI_SSIENR     0x08
#define RP2040_SSI_SER        0x10
#define RP2040_SSI_BAUDR      0x14
#define RP2040_SSI_TXFTLR     0x18
#define RP2040_SSI_RXFTLR     0x1c
#define RP2040_SSI_TXFLR      0x20
#define RP2040_SSI_RXFLR      0x24
#define RP2040_SSI_SR         0x28
#define RP2040_SSI_IMR        0x2c
#define RP2040_SSI_ISR        0x30
#define RP2040_SSI_RISR       0x34
#define RP2040_SSI_TXOICR     0x38
#define RP2040_SSI_RXOICR     0x3c
#define RP2040_SSI_RXUICR     0x40
#define RP2040_SSI_MSTICR     0x44
#define RP2040_SSI_ICR        0x48
#define RP2040_SSI_DMACR      0x4c
#define RP2040_SSI_DMATDLR    0x50
#define RP2040_SSI_DMARDLR    0x54
#define RP2040_SSI_IDR        0x58
#define RP2040_SSI_VERSION_ID 0x5c
#define RP2040_SSI_DR0        0x60
#define RP2040_SSI_DR_END     0xec
#define RP2040_SSI_RX_SAMPLE_DLY 0xf0
#define RP2040_SSI_SPI_CTRLR0 0xf4

#define RP2040_SSI_SR_BUSY 0x01
#define RP2040_SSI_SR_TFNF 0x02
#define RP2040_SSI_SR_TFE  0x04
#define RP2040_SSI_SR_RFNE 0x08
#define RP2040_SSI_SR_RFF  0x10

#define FLASH_CMD_READ         0x03
#define FLASH_CMD_WRITE_STATUS 0x01
#define FLASH_CMD_PAGE_PROGRAM 0x02
#define FLASH_CMD_READ_STATUS  0x05
#define FLASH_CMD_READ_STATUS2 0x35
#define FLASH_CMD_WRITE_ENABLE 0x06
#define FLASH_CMD_READ_UNIQUE_ID 0x4b
#define FLASH_CMD_SECTOR_ERASE 0x20
#define FLASH_CMD_QUAD_IO_READ 0xeb
#define FLASH_CMD_CONTINUATION_READ 0xa0

#define RP2040_SSI_DMACR_TDMAE BIT(1)
#define RP2040_SSI_DMACR_RDMAE BIT(0)

#define FLASH_UNIQUE_ID_SIZE 8
#define FLASH_UNIQUE_ID_DUMMY_BYTES 4
#define FLASH_UID_DEFAULT 0x3eb8a7493fcc0608ull

#define FLASH_STATUS_WIP 0x01
#define FLASH_STATUS_WEL 0x02
#define FLASH_PAGE_SIZE  256
#define FLASH_SECTOR_SIZE 4096

#define UF2_MAGIC_START0 0x0a324655
#define UF2_MAGIC_START1 0x9e5d5157
#define UF2_MAGIC_END    0x0ab16f30
#define UF2_BLOCK_SIZE   512
#define UF2_HEADER_SIZE  32
#define UF2_MAX_PAYLOAD  476

#define UF2_FLAG_NOT_MAIN_FLASH       BIT(0)
#define UF2_FLAG_FAMILY_ID_PRESENT    BIT(13)
#define UF2_RP2040_FAMILY_ID          0xe48bff56

#define ATOMIC_ALIAS_MASK 0x3000
#define ATOMIC_XOR        0x1000
#define ATOMIC_SET        0x2000
#define ATOMIC_CLR        0x3000

#define RP2040_BOOT2_SIZE 256
#define RP2040_BOOT2_CRC_SIZE 252
#define RP2040_BOOT2_CRC_INIT 0xffffffff
#define RP2040_BOOT2_CRC_POLY 0x04c11db7
#define RP2040_SRAM_BASE 0x20000000
#define RP2040_SRAM_END  0x20042000

static uint32_t rp2040_xip_apply_alias(uint32_t old, uint32_t value,
                                       hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static void rp2040_xip_rx_clear(RP2040XipState *s)
{
    s->rx_len = 0;
    s->rx_pos = 0;
}

static void rp2040_xip_rx_push(RP2040XipState *s, uint8_t value)
{
    if (s->rx_len == ARRAY_SIZE(s->rx) && s->rx_pos > 0) {
        memmove(s->rx, s->rx + s->rx_pos, s->rx_len - s->rx_pos);
        s->rx_len -= s->rx_pos;
        s->rx_pos = 0;
    }

    if (s->rx_len < ARRAY_SIZE(s->rx)) {
        s->rx[s->rx_len++] = value;
        qemu_irq_pulse(s->dreq_rx);
    }
}

static bool rp2040_xip_rx_compact(RP2040XipState *s)
{
    if (s->rx_len == s->rx_pos) {
        rp2040_xip_rx_clear(s);
        return true;
    }
    if (s->rx_pos > 0) {
        memmove(s->rx, s->rx + s->rx_pos, s->rx_len - s->rx_pos);
        s->rx_len -= s->rx_pos;
        s->rx_pos = 0;
        return true;
    }
    return false;
}

static bool rp2040_xip_flash_word(RP2040XipState *s, uint32_t addr,
                                  uint32_t *value)
{
    if (s->flash_size < sizeof(uint32_t) ||
        addr > s->flash_size - sizeof(uint32_t)) {
        *value = 0xffffffff;
        return false;
    }

    *value = ldl_le_p(s->storage + addr);
    return true;
}

static void rp2040_xip_rx_push_ssi_word(RP2040XipState *s, uint32_t value)
{
    if (ARRAY_SIZE(s->rx) - (s->rx_len - s->rx_pos) < sizeof(uint32_t)) {
        rp2040_xip_rx_compact(s);
    }
    if (ARRAY_SIZE(s->rx) - s->rx_len < sizeof(uint32_t)) {
        return;
    }

    /*
     * Non-XIP 32-bit SSI reads deliver the serial flash byte stream in the
     * opposite byte order expected by the RP2040 system bus; SDK users enable
     * DMA BSWAP when copying words from SSI_DR0.
     */
    s->rx[s->rx_len++] = extract32(value, 24, 8);
    s->rx[s->rx_len++] = extract32(value, 16, 8);
    s->rx[s->rx_len++] = extract32(value, 8, 8);
    s->rx[s->rx_len++] = extract32(value, 0, 8);
    qemu_irq_pulse(s->dreq_rx);
}

static void rp2040_xip_ssi_bulk_fill(RP2040XipState *s)
{
    while (s->ssi_bulk_remaining > 0 &&
           ARRAY_SIZE(s->rx) - (s->rx_len - s->rx_pos) >=
           sizeof(uint32_t)) {
        uint32_t value;

        rp2040_xip_rx_compact(s);
        rp2040_xip_flash_word(s, s->ssi_bulk_addr, &value);
        rp2040_xip_rx_push_ssi_word(s, value);
        s->ssi_bulk_addr += sizeof(uint32_t);
        s->ssi_bulk_remaining--;
    }
}

static void rp2040_xip_ssi_bulk_start(RP2040XipState *s, uint32_t addr,
                                      uint32_t words)
{
    rp2040_xip_rx_clear(s);
    s->ssi_bulk_addr = addr;
    s->ssi_bulk_remaining = words;
    rp2040_xip_ssi_bulk_fill(s);
}

static void rp2040_xip_update_stream_dreq(RP2040XipState *s)
{
    qemu_set_irq(s->dreq_stream, s->stream_fifo_len > s->stream_fifo_pos);
}

static void rp2040_xip_stream_clear(RP2040XipState *s)
{
    s->stream_fifo_len = 0;
    s->stream_fifo_pos = 0;
    rp2040_xip_update_stream_dreq(s);
}

static uint32_t rp2040_xip_stream_word(RP2040XipState *s)
{
    uint32_t off;

    if (s->flash_size < sizeof(uint32_t)) {
        return 0xffffffff;
    }
    if (s->stream_addr < RP2040_XIP_FLASH_BASE ||
        s->stream_addr - RP2040_XIP_FLASH_BASE >
        s->flash_size - sizeof(uint32_t)) {
        return 0xffffffff;
    }

    off = s->stream_addr - RP2040_XIP_FLASH_BASE;
    return ldl_le_p(s->storage + off);
}

static void rp2040_xip_stream_fill(RP2040XipState *s)
{
    if (s->stream_fifo_pos == s->stream_fifo_len) {
        s->stream_fifo_pos = 0;
        s->stream_fifo_len = 0;
    }

    while (s->stream_ctr > 0 &&
           s->stream_fifo_len < ARRAY_SIZE(s->stream_fifo)) {
        s->stream_fifo[s->stream_fifo_len++] = rp2040_xip_stream_word(s);
        s->stream_addr += 4;
        s->stream_ctr--;
    }

    rp2040_xip_update_stream_dreq(s);
}

static uint32_t rp2040_xip_stream_pop(RP2040XipState *s)
{
    uint32_t value = 0;

    rp2040_xip_stream_fill(s);
    if (s->stream_fifo_pos < s->stream_fifo_len) {
        value = s->stream_fifo[s->stream_fifo_pos++];
    }
    rp2040_xip_stream_fill(s);
    return value;
}

static uint32_t rp2040_xip_stat(RP2040XipState *s)
{
    uint32_t stat = RP2040_XIP_STAT_FLUSH_READY;

    rp2040_xip_stream_fill(s);
    if (s->stream_fifo_pos == s->stream_fifo_len) {
        stat |= RP2040_XIP_STAT_FIFO_EMPTY;
    }
    if (s->stream_fifo_len - s->stream_fifo_pos ==
        ARRAY_SIZE(s->stream_fifo)) {
        stat |= RP2040_XIP_STAT_FIFO_FULL;
    }

    return stat;
}

static uint8_t rp2040_xip_status(RP2040XipState *s)
{
    uint8_t status = 0;

    if (s->busy) {
        status |= FLASH_STATUS_WIP;
    }
    if (s->write_enable) {
        status |= FLASH_STATUS_WEL;
    }

    return status;
}

static uint32_t rp2040_xip_tx_addr(RP2040XipState *s)
{
    return (uint32_t)s->tx[1] << 16 | s->tx[2] << 8 | s->tx[3];
}

static uint32_t rp2040_xip_quad_io_addr(RP2040XipState *s)
{
    return (uint32_t)s->tx[1] << 16 | s->tx[2] << 8 | s->tx[3];
}

static uint8_t rp2040_xip_flash_uid_byte(RP2040XipState *s, unsigned index)
{
    return extract64(s->flash_uid, (FLASH_UNIQUE_ID_SIZE - 1 - index) * 8, 8);
}

static void rp2040_xip_finish_busy(RP2040XipState *s)
{
    s->busy = false;
}

static void rp2040_xip_reset_tx(RP2040XipState *s)
{
    s->tx_len = 0;
    s->tx_unsupported_logged = false;
    s->ssi_bulk_remaining = 0;
}

static bool rp2040_xip_writeback(RP2040XipState *s, Error **errp)
{
    g_autoptr(GError) gerr = NULL;

    if (!s->flash_file || !*s->flash_file) {
        return true;
    }

    if (!g_file_set_contents(s->flash_file, (const char *)s->storage,
                             s->flash_size, &gerr)) {
        error_setg(errp, "could not write flash file '%s': %s",
                   s->flash_file, gerr->message);
        return false;
    }

    return true;
}

static void rp2040_xip_writeback_or_warn(RP2040XipState *s)
{
    Error *local_err = NULL;

    if (!rp2040_xip_writeback(s, &local_err)) {
        warn_report_err(local_err);
    }
}

static uint32_t rp2040_xip_boot2_crc(const uint8_t *data)
{
    uint32_t crc = RP2040_BOOT2_CRC_INIT;
    int i;
    int bit;

    for (i = 0; i < RP2040_BOOT2_CRC_SIZE; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (bit = 0; bit < 8; bit++) {
            if (crc & BIT(31)) {
                crc = (crc << 1) ^ RP2040_BOOT2_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static bool rp2040_xip_boot2_empty(const uint8_t *data)
{
    int i;

    for (i = 0; i < RP2040_BOOT2_SIZE; i++) {
        if (data[i] != 0xff) {
            return false;
        }
    }

    return true;
}

static bool rp2040_xip_boot2_looks_like_vector_table(const uint8_t *data)
{
    uint32_t initial_sp = ldl_le_p(data);
    uint32_t reset = ldl_le_p(data + 4);

    return initial_sp >= RP2040_SRAM_BASE &&
           initial_sp <= RP2040_SRAM_END &&
           reset >= RP2040_XIP_FLASH_BASE &&
           reset < RP2040_XIP_FLASH_BASE + MiB &&
           (reset & 1);
}

static bool rp2040_xip_fixup_boot2(RP2040XipState *s, const char *filename,
                                   Error **errp)
{
    uint32_t crc;

    if (s->flash_size < RP2040_BOOT2_SIZE) {
        error_setg(errp, "flash is too small for an RP2040 boot2 block");
        return false;
    }

    if (rp2040_xip_boot2_empty(s->storage)) {
        error_setg(errp, "image '%s' does not contain RP2040 boot2 at "
                   "0x%08x", filename, RP2040_XIP_FLASH_BASE);
        return false;
    }

    if (rp2040_xip_boot2_looks_like_vector_table(s->storage)) {
        error_setg(errp, "image '%s' starts with an application vector table, "
                   "not RP2040 boot2; use a Pico SDK UF2 or an ELF with "
                   "boot2 linked at 0x%08x", filename, RP2040_XIP_FLASH_BASE);
        return false;
    }

    crc = rp2040_xip_boot2_crc(s->storage);
    stl_le_p(s->storage + RP2040_BOOT2_CRC_SIZE, crc);

    return true;
}

static void rp2040_xip_program(RP2040XipState *s)
{
    uint32_t addr;
    uint32_t page_end;
    unsigned data_len;
    unsigned i;

    trace_rp2040_xip_program(s->tx_len >= 4 ? rp2040_xip_tx_addr(s) : 0,
                             s->tx_len > 4 ? s->tx_len - 4 : 0,
                             s->write_enable);

    if (!s->write_enable) {
        return;
    }

    s->write_enable = false;

    if (s->tx_len <= 4) {
        return;
    }

    addr = rp2040_xip_tx_addr(s);
    if (addr >= s->flash_size) {
        return;
    }

    page_end = ROUND_UP(addr + 1, FLASH_PAGE_SIZE);
    data_len = MIN(s->tx_len - 4, page_end - addr);
    data_len = MIN(data_len, s->flash_size - addr);

    for (i = 0; i < data_len; i++) {
        s->storage[addr + i] &= s->tx[4 + i];
    }

    rp2040_xip_writeback_or_warn(s);
    s->busy = true;
}

static void rp2040_xip_erase(RP2040XipState *s)
{
    uint32_t addr;
    uint32_t base;

    trace_rp2040_xip_erase(s->tx_len >= 4 ? rp2040_xip_tx_addr(s) : 0,
                           s->write_enable);

    if (!s->write_enable) {
        return;
    }

    s->write_enable = false;

    if (s->tx_len < 4) {
        return;
    }

    addr = rp2040_xip_tx_addr(s);
    base = QEMU_ALIGN_DOWN(addr, FLASH_SECTOR_SIZE);
    if (base >= s->flash_size) {
        return;
    }

    memset(&s->storage[base], 0xff, MIN(FLASH_SECTOR_SIZE,
                                       s->flash_size - base));
    rp2040_xip_writeback_or_warn(s);
    s->busy = true;
}

static void rp2040_xip_finish_command(RP2040XipState *s)
{
    if (s->tx_len == 0) {
        return;
    }

    trace_rp2040_xip_finish_command(s->tx[0], s->tx_len);

    switch (s->tx[0]) {
    case FLASH_CMD_WRITE_STATUS:
        s->write_enable = false;
        break;
    case FLASH_CMD_PAGE_PROGRAM:
        rp2040_xip_program(s);
        break;
    case FLASH_CMD_SECTOR_ERASE:
        rp2040_xip_erase(s);
        break;
    default:
        break;
    }

    rp2040_xip_reset_tx(s);
}

static void rp2040_xip_dr_write(RP2040XipState *s, uint8_t value)
{
    uint32_t addr;

    if (s->tx_len < ARRAY_SIZE(s->tx)) {
        s->tx[s->tx_len++] = value;
    }

    switch (s->tx[0]) {
    case FLASH_CMD_WRITE_STATUS:
        s->write_enable = false;
        rp2040_xip_rx_push(s, 0);
        break;
    case FLASH_CMD_PAGE_PROGRAM:
    case FLASH_CMD_SECTOR_ERASE:
        rp2040_xip_rx_push(s, 0);
        break;
    case FLASH_CMD_WRITE_ENABLE:
        s->write_enable = true;
        rp2040_xip_rx_push(s, 0);
        rp2040_xip_reset_tx(s);
        break;
    case FLASH_CMD_READ_STATUS:
        rp2040_xip_rx_push(s, rp2040_xip_status(s));
        rp2040_xip_finish_busy(s);
        rp2040_xip_reset_tx(s);
        break;
    case FLASH_CMD_READ_STATUS2:
        rp2040_xip_rx_push(s, 0);
        rp2040_xip_finish_busy(s);
        rp2040_xip_reset_tx(s);
        break;
    case FLASH_CMD_READ_UNIQUE_ID:
        if (s->tx_len <= 1 + FLASH_UNIQUE_ID_DUMMY_BYTES) {
            rp2040_xip_rx_push(s, 0);
        } else {
            unsigned index = s->tx_len - 2 - FLASH_UNIQUE_ID_DUMMY_BYTES;

            rp2040_xip_rx_push(s, index < FLASH_UNIQUE_ID_SIZE ?
                               rp2040_xip_flash_uid_byte(s, index) : 0);
        }
        break;
    case FLASH_CMD_READ:
        if (s->tx_len <= 4) {
            rp2040_xip_rx_push(s, 0);
        } else {
            addr = rp2040_xip_tx_addr(s) + s->tx_len - 5;
            rp2040_xip_rx_push(s, addr < s->flash_size ?
                               s->storage[addr] : 0xff);
        }
        break;
    case FLASH_CMD_QUAD_IO_READ:
        /*
         * Minimal 0xeb fast-read support for the RP2040 mask ROM path.
         * The ROM clocks opcode, 24-bit address and mode/dummy bytes before
         * consuming data. We do not model bus width or wait-cycle timing here.
         */
        if (s->tx_len <= 5) {
            rp2040_xip_rx_push(s, 0);
        } else {
            addr = rp2040_xip_quad_io_addr(s) + s->tx_len - 6;
            rp2040_xip_rx_push(s, addr < s->flash_size ?
                               s->storage[addr] : 0xff);
        }
        break;
    case 0x00:
        rp2040_xip_rx_push(s, 0);
        break;
    default:
        /*
         * The RP2040 boot ROM performs small full-duplex SSI transactions
         * while probing the flash path. Even for commands we do not model yet,
         * a transmitted byte clocks one receive byte back from the bus.
         */
        if (!s->tx_unsupported_logged) {
            g_autofree char *detail = g_strdup_printf("opcode 0x%02x",
                                                      s->tx[0]);

            rp2040_log_nyi("xip.ssi", "flash command", detail);
            s->tx_unsupported_logged = true;
        }
        rp2040_xip_rx_push(s, 0);
        break;
    }
}

static MemTxResult rp2040_xip_read_common(RP2040XipState *s, hwaddr addr,
                                          uint64_t *data, unsigned size,
                                          bool synthetic_vector_overlay)
{
    uint64_t value = 0;
    unsigned i;

    if (s->busy || addr + size > s->flash_size) {
        return MEMTX_ERROR;
    }

    for (i = 0; i < size; i++) {
        hwaddr cur = addr + i;
        uint8_t byte = s->storage[cur];

        /*
         * Synthetic ROM CI exit support: let firmware copy a HardFault vector
         * that points back into ROM without mutating persistent flash storage.
         */
        if (synthetic_vector_overlay &&
            s->synthetic_hardfault_vector_enabled &&
            cur >= RP2040_BOOT2_SIZE + 0x0c &&
            cur < RP2040_BOOT2_SIZE + 0x10) {
            byte = extract32(s->synthetic_hardfault_vector,
                             (cur - (RP2040_BOOT2_SIZE + 0x0c)) * 8, 8);
        }
        value |= (uint64_t)byte << (i * 8);
    }
    *data = value;
    return MEMTX_OK;
}

static MemTxResult rp2040_xip_read(void *opaque, hwaddr addr, uint64_t *data,
                                   unsigned size, MemTxAttrs attrs)
{
    return rp2040_xip_read_common(opaque, addr, data, size, true);
}

static MemTxResult rp2040_xip_alias_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    return rp2040_xip_read_common(opaque, addr, data, size, false);
}

static MemTxResult rp2040_xip_write(void *opaque, hwaddr addr, uint64_t data,
                                    unsigned size, MemTxAttrs attrs)
{
    RP2040XipState *s = opaque;
    unsigned i;

    if (!s->xip_writable) {
        return MEMTX_ERROR;
    }
    if (addr + size > s->flash_size) {
        return MEMTX_ERROR;
    }

    for (i = 0; i < size; i++) {
        s->storage[addr + i] = extract64(data, i * 8, 8);
    }
    return MEMTX_OK;
}

static uint64_t rp2040_xip_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040XipState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case RP2040_XIP_CTRL:
        value = s->xip_ctrl;
        break;
    case RP2040_XIP_FLUSH:
    case RP2040_XIP_CTR_HIT:
    case RP2040_XIP_CTR_ACC:
        value = 0;
        break;
    case RP2040_XIP_STAT:
        value = rp2040_xip_stat(s);
        break;
    case RP2040_XIP_STREAM_ADDR:
        value = s->stream_addr;
        break;
    case RP2040_XIP_STREAM_CTR:
        value = s->stream_ctr;
        break;
    case RP2040_XIP_STREAM_FIFO:
        value = rp2040_xip_stream_pop(s);
        break;
    default:
        value = 0;
        qemu_log_mask(LOG_UNIMP, "rp2040.xip.ctrl: unimplemented read  "
                      "(size %d, addr 0x%08" HWADDR_PRIx
                      ", offset 0x%04" HWADDR_PRIx
                      ") -> 0x%0*" PRIx64 "\n",
                      size, RP2040_XIP_CTRL_BASE + addr, offset,
                      size << 1, value);
        break;
    }

    return value;
}

static void rp2040_xip_ctrl_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    RP2040XipState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t new_value;

    switch (offset) {
    case RP2040_XIP_CTRL:
        new_value = rp2040_xip_apply_alias(s->xip_ctrl, value, alias);
        s->xip_ctrl = new_value & (RP2040_XIP_CTRL_EN |
                                   RP2040_XIP_CTRL_ERR_BADWRITE);
        break;
    case RP2040_XIP_FLUSH:
        rp2040_xip_stream_clear(s);
        break;
    case RP2040_XIP_CTR_HIT:
    case RP2040_XIP_CTR_ACC:
        break;
    case RP2040_XIP_STREAM_ADDR:
        s->stream_addr = value & ~3u;
        break;
    case RP2040_XIP_STREAM_CTR:
        s->stream_ctr = value & RP2040_XIP_STREAM_CTR_MASK;
        if (s->stream_ctr == 0) {
            rp2040_xip_stream_clear(s);
        } else {
            rp2040_xip_stream_fill(s);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rp2040.xip.ctrl: unimplemented write "
                      "(size %d, addr 0x%08" HWADDR_PRIx
                      ", offset 0x%04" HWADDR_PRIx
                      ", value 0x%0*" PRIx64 ")\n",
                      size, RP2040_XIP_CTRL_BASE + addr, offset,
                      size << 1, value);
        break;
    }
}

static uint64_t rp2040_xip_aux_read(void *opaque, hwaddr addr, unsigned size)
{
    return rp2040_xip_stream_pop(opaque);
}

static void rp2040_xip_aux_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    rp2040_log_nyi("xip.aux", "write",
                   "XIP auxiliary stream FIFO is read-only");
}

static uint64_t rp2040_xip_ssi_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040XipState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint8_t value;
    uint32_t risr = s->rx_len > s->rx_pos ? 0 : 1;
    uint64_t ret;

    if (offset >= RP2040_SSI_DR0 && offset <= RP2040_SSI_DR_END) {
        unsigned i;

        ret = 0;
        for (i = 0; i < size; i++) {
            if (s->rx_pos == s->rx_len) {
                rp2040_xip_rx_clear(s);
                rp2040_xip_ssi_bulk_fill(s);
            }
            if (s->rx_pos < s->rx_len) {
                value = s->rx[s->rx_pos++];
            } else {
                value = 0;
            }
            ret |= (uint64_t)value << (i * 8);
        }
        if (s->rx_pos == s->rx_len) {
            rp2040_xip_rx_clear(s);
            rp2040_xip_ssi_bulk_fill(s);
        }
        return ret;
    }

    switch (offset) {
    case RP2040_SSI_CTRLR0:
        ret = s->ctrlr0;
        break;
    case RP2040_SSI_CTRLR1:
        ret = s->ctrlr1;
        break;
    case RP2040_SSI_SSIENR:
        ret = s->ssienr;
        break;
    case RP2040_SSI_SER:
        ret = s->ser;
        break;
    case RP2040_SSI_BAUDR:
        ret = s->baudr;
        break;
    case RP2040_SSI_TXFTLR:
        ret = s->txftlr;
        break;
    case RP2040_SSI_RXFTLR:
        ret = s->rxftlr;
        break;
    case RP2040_SSI_TXFLR:
        ret = 0;
        break;
    case RP2040_SSI_RXFLR:
        ret = s->rx_len - s->rx_pos;
        break;
    case RP2040_SSI_SR:
        ret = RP2040_SSI_SR_TFE | RP2040_SSI_SR_TFNF |
              (s->busy ? RP2040_SSI_SR_BUSY : 0) |
              (s->rx_len > s->rx_pos ? RP2040_SSI_SR_RFNE : 0) |
              (s->rx_len - s->rx_pos == ARRAY_SIZE(s->rx) ?
               RP2040_SSI_SR_RFF : 0);
        break;
    case RP2040_SSI_IMR:
        ret = s->imr;
        break;
    case RP2040_SSI_ISR:
    case RP2040_SSI_RISR:
        ret = risr;
        break;
    case RP2040_SSI_TXOICR:
    case RP2040_SSI_RXOICR:
    case RP2040_SSI_RXUICR:
    case RP2040_SSI_MSTICR:
    case RP2040_SSI_ICR:
        ret = 0;
        break;
    case RP2040_SSI_DMACR:
        ret = s->dmacr;
        break;
    case RP2040_SSI_DMATDLR:
        ret = s->dmatdlr;
        break;
    case RP2040_SSI_DMARDLR:
        ret = s->dmardlr;
        break;
    case RP2040_SSI_IDR:
        ret = 0;
        break;
    case RP2040_SSI_VERSION_ID:
        ret = 0x3430312a;
        break;
    case RP2040_SSI_RX_SAMPLE_DLY:
        ret = s->rx_sample_dly;
        break;
    case RP2040_SSI_SPI_CTRLR0:
        ret = s->spi_ctrlr0;
        break;
    default:
        ret = 0;
        qemu_log_mask(LOG_UNIMP, "rp2040.xip.ssi: unimplemented read  "
                      "(size %d, addr 0x%08" HWADDR_PRIx
                      ", offset 0x%04" HWADDR_PRIx
                      ") -> 0x%0*" PRIx64 "\n",
                      size, RP2040_XIP_SSI_BASE + addr, offset,
                      size << 1, ret);
        break;
    }

    return ret;
}

static void rp2040_xip_ssi_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
    RP2040XipState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t old_ssienr = s->ssienr;
    uint32_t old_ser = s->ser;
    uint32_t new_value;

    if (offset >= RP2040_SSI_DR0 && offset <= RP2040_SSI_DR_END) {
        if (s->tx_len < 8 || (s->tx_len & 0x3f) == 0) {
            trace_rp2040_xip_dr_write(value, size, s->tx_len, s->ctrlr0,
                                      s->ctrlr1, s->spi_ctrlr0);
        }
        if (size == 4 && (value & 0xff) == FLASH_CMD_CONTINUATION_READ) {
            rp2040_xip_ssi_bulk_start(s, value >> 8, s->ctrlr1 + 1);
            return;
        }
        rp2040_xip_dr_write(s, value & 0xff);
        return;
    }

    switch (offset) {
    case RP2040_SSI_CTRLR0:
        s->ctrlr0 = rp2040_xip_apply_alias(s->ctrlr0, value, alias);
        trace_rp2040_xip_ctrlr0(s->ctrlr0);
        break;
    case RP2040_SSI_CTRLR1:
        s->ctrlr1 = rp2040_xip_apply_alias(s->ctrlr1, value, alias);
        trace_rp2040_xip_ctrlr1(s->ctrlr1);
        break;
    case RP2040_SSI_SSIENR:
        new_value = rp2040_xip_apply_alias(s->ssienr, value, alias);
        s->ssienr = new_value & 1;
        trace_rp2040_xip_ssienr(old_ssienr, s->ssienr, s->tx_len);
        if ((old_ssienr & 1) && !s->ssienr) {
            rp2040_xip_finish_command(s);
        }
        if (!s->ssienr) {
            rp2040_xip_rx_clear(s);
            rp2040_xip_reset_tx(s);
        }
        break;
    case RP2040_SSI_SER:
        new_value = rp2040_xip_apply_alias(s->ser, value, alias);
        s->ser = new_value & 1;
        trace_rp2040_xip_ser(old_ser, s->ser, s->tx_len);
        if ((old_ser & 1) && !s->ser) {
            rp2040_xip_finish_command(s);
        }
        break;
    case RP2040_SSI_BAUDR:
        new_value = rp2040_xip_apply_alias(s->baudr, value, alias);
        s->baudr = new_value & 0xffff;
        break;
    case RP2040_SSI_TXFTLR:
        new_value = rp2040_xip_apply_alias(s->txftlr, value, alias);
        s->txftlr = new_value & 0xff;
        break;
    case RP2040_SSI_RXFTLR:
        new_value = rp2040_xip_apply_alias(s->rxftlr, value, alias);
        s->rxftlr = new_value & 0xff;
        break;
    case RP2040_SSI_IMR:
        new_value = rp2040_xip_apply_alias(s->imr, value, alias);
        s->imr = new_value & 0x3f;
        break;
    case RP2040_SSI_DMACR:
        new_value = rp2040_xip_apply_alias(s->dmacr, value, alias);
        s->dmacr = new_value & (RP2040_SSI_DMACR_TDMAE |
                                RP2040_SSI_DMACR_RDMAE);
        break;
    case RP2040_SSI_DMATDLR:
        s->dmatdlr = rp2040_xip_apply_alias(s->dmatdlr, value, alias) & 0xff;
        break;
    case RP2040_SSI_DMARDLR:
        s->dmardlr = rp2040_xip_apply_alias(s->dmardlr, value, alias) & 0xff;
        break;
    case RP2040_SSI_RX_SAMPLE_DLY:
        s->rx_sample_dly = rp2040_xip_apply_alias(s->rx_sample_dly, value,
                                                  alias) & 0xff;
        break;
    case RP2040_SSI_SPI_CTRLR0:
        s->spi_ctrlr0 = rp2040_xip_apply_alias(s->spi_ctrlr0, value, alias);
        trace_rp2040_xip_spi_ctrlr0(s->spi_ctrlr0);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rp2040.xip.ssi: unimplemented write "
                      "(size %d, addr 0x%08" HWADDR_PRIx
                      ", offset 0x%04" HWADDR_PRIx
                      ", value 0x%0*" PRIx64 ")\n",
                      size, RP2040_XIP_SSI_BASE + addr, offset,
                      size << 1, value);
        break;
    }
}

static const MemoryRegionOps rp2040_xip_ops = {
    .read_with_attrs = rp2040_xip_read,
    .write_with_attrs = rp2040_xip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static const MemoryRegionOps rp2040_xip_alias_ops = {
    .read_with_attrs = rp2040_xip_alias_read,
    .write_with_attrs = rp2040_xip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static const MemoryRegionOps rp2040_xip_ctrl_ops = {
    .read = rp2040_xip_ctrl_read,
    .write = rp2040_xip_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps rp2040_xip_ssi_ops = {
    .read = rp2040_xip_ssi_read,
    .write = rp2040_xip_ssi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static const MemoryRegionOps rp2040_xip_aux_ops = {
    .read = rp2040_xip_aux_read,
    .write = rp2040_xip_aux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void rp2040_xip_set_writable(RP2040XipState *s, bool writable)
{
    s->xip_writable = writable;
}

void rp2040_xip_set_synthetic_hardfault_vector(RP2040XipState *s,
                                               uint32_t handler)
{
    s->synthetic_hardfault_vector = handler;
    s->synthetic_hardfault_vector_enabled = true;
}

void rp2040_xip_qspi_cs(RP2040XipState *s, bool high)
{
    if (s->qspi_cs_high == high) {
        return;
    }

    trace_rp2040_xip_qspi_cs(high, s->tx_len);
    s->qspi_cs_high = high;

    if (high) {
        rp2040_xip_finish_command(s);
    } else {
        rp2040_xip_rx_clear(s);
        rp2040_xip_reset_tx(s);
    }
}

static bool rp2040_xip_load_elf(RP2040XipState *s, const char *filename,
                                Error **errp)
{
    g_autofree gchar *contents = NULL;
    gsize len;
    const Elf32_Ehdr *ehdr;
    const Elf32_Phdr *phdr;
    int i;

    if (!g_file_get_contents(filename, &contents, &len, NULL)) {
        error_setg(errp, "could not load flash image '%s'", filename);
        return true;
    }

    if (len < sizeof(*ehdr)) {
        return false;
    }

    ehdr = (const Elf32_Ehdr *)contents;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        return false;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        le16_to_cpu(ehdr->e_machine) != EM_ARM) {
        error_setg(errp, "unsupported flash ELF image '%s'", filename);
        return true;
    }
    if (le32_to_cpu(ehdr->e_phoff) > len ||
        le16_to_cpu(ehdr->e_phentsize) != sizeof(*phdr) ||
        le16_to_cpu(ehdr->e_phnum) >
        (len - le32_to_cpu(ehdr->e_phoff)) / sizeof(*phdr)) {
        error_setg(errp, "invalid flash ELF image '%s'", filename);
        return true;
    }

    phdr = (const Elf32_Phdr *)(contents + le32_to_cpu(ehdr->e_phoff));
    for (i = 0; i < le16_to_cpu(ehdr->e_phnum); i++) {
        uint32_t paddr = le32_to_cpu(phdr[i].p_paddr);
        uint32_t filesz = le32_to_cpu(phdr[i].p_filesz);
        uint32_t memsz = le32_to_cpu(phdr[i].p_memsz);
        uint32_t off = le32_to_cpu(phdr[i].p_offset);
        uint32_t xip_off;

        if (le32_to_cpu(phdr[i].p_type) != PT_LOAD) {
            continue;
        }

        if (filesz == 0) {
            continue;
        }

        if (paddr < RP2040_XIP_FLASH_BASE ||
            paddr - RP2040_XIP_FLASH_BASE > s->flash_size ||
            filesz > memsz ||
            filesz > s->flash_size - (paddr - RP2040_XIP_FLASH_BASE) ||
            off > len ||
            filesz > len - off) {
            error_setg(errp, "flash ELF segment is outside XIP storage");
            return true;
        }

        xip_off = paddr - RP2040_XIP_FLASH_BASE;
        memcpy(s->storage + xip_off, contents + off, filesz);
    }

    return true;
}

static bool rp2040_xip_load_uf2(RP2040XipState *s, const char *filename,
                                Error **errp)
{
    g_autofree gchar *contents = NULL;
    gsize len;
    unsigned blocks;
    unsigned i;
    bool copied = false;

    if (!g_file_get_contents(filename, &contents, &len, NULL)) {
        error_setg(errp, "could not load flash image '%s'", filename);
        return true;
    }

    if (len < UF2_BLOCK_SIZE ||
        ldl_le_p(contents) != UF2_MAGIC_START0 ||
        ldl_le_p(contents + 4) != UF2_MAGIC_START1) {
        return false;
    }

    if (len % UF2_BLOCK_SIZE != 0) {
        error_setg(errp, "invalid UF2 image '%s': size is not a multiple "
                   "of %u bytes", filename, UF2_BLOCK_SIZE);
        return true;
    }

    blocks = len / UF2_BLOCK_SIZE;
    for (i = 0; i < blocks; i++) {
        const uint8_t *block = (uint8_t *)contents + i * UF2_BLOCK_SIZE;
        uint32_t flags;
        uint32_t target;
        uint32_t payload_size;
        uint32_t family_id;
        uint32_t offset;

        if (ldl_le_p(block) != UF2_MAGIC_START0 ||
            ldl_le_p(block + 4) != UF2_MAGIC_START1 ||
            ldl_le_p(block + UF2_BLOCK_SIZE - 4) != UF2_MAGIC_END) {
            error_setg(errp, "invalid UF2 image '%s': bad magic in block %u",
                       filename, i);
            return true;
        }

        flags = ldl_le_p(block + 8);
        if (flags & UF2_FLAG_NOT_MAIN_FLASH) {
            continue;
        }

        if (!(flags & UF2_FLAG_FAMILY_ID_PRESENT)) {
            error_setg(errp, "invalid RP2040 UF2 image '%s': block %u has "
                       "no family ID", filename, i);
            return true;
        }

        family_id = ldl_le_p(block + 28);
        if (family_id != UF2_RP2040_FAMILY_ID) {
            error_setg(errp, "unsupported UF2 family ID 0x%08" PRIx32
                       " in '%s'", family_id, filename);
            return true;
        }

        target = ldl_le_p(block + 12);
        payload_size = ldl_le_p(block + 16);
        if (payload_size > UF2_MAX_PAYLOAD ||
            target < RP2040_XIP_FLASH_BASE ||
            target - RP2040_XIP_FLASH_BASE > s->flash_size ||
            payload_size > s->flash_size -
                           (target - RP2040_XIP_FLASH_BASE)) {
            error_setg(errp, "UF2 block %u in '%s' is outside XIP flash",
                       i, filename);
            return true;
        }

        offset = target - RP2040_XIP_FLASH_BASE;
        memcpy(s->storage + offset, block + UF2_HEADER_SIZE, payload_size);
        copied = true;
    }

    if (!copied) {
        error_setg(errp, "UF2 image '%s' contains no RP2040 flash payload",
                   filename);
    }

    return true;
}

void rp2040_xip_load_image(RP2040XipState *s, const char *filename,
                           Error **errp)
{
    Error *local_err = NULL;
    ssize_t image_size;

    if (!filename) {
        return;
    }

    if (rp2040_xip_load_elf(s, filename, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        if (!rp2040_xip_fixup_boot2(s, filename, errp)) {
            return;
        }
        rp2040_xip_writeback(s, errp);
        return;
    }

    if (rp2040_xip_load_uf2(s, filename, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        if (!rp2040_xip_fixup_boot2(s, filename, errp)) {
            return;
        }
        rp2040_xip_writeback(s, errp);
        return;
    }

    image_size = load_image_size(filename, s->storage, s->flash_size);
    if (image_size < 0) {
        error_setg(errp, "could not load flash image '%s'", filename);
        return;
    }
    if (!rp2040_xip_fixup_boot2(s, filename, errp)) {
        return;
    }
    rp2040_xip_writeback(s, errp);
}

bool rp2040_xip_flash_range_erase(RP2040XipState *s, uint32_t flash_offs,
                                  uint32_t count, uint32_t block_size,
                                  uint8_t block_cmd, Error **errp)
{
    if (!QEMU_IS_ALIGNED(flash_offs, FLASH_SECTOR_SIZE) ||
        !QEMU_IS_ALIGNED(count, FLASH_SECTOR_SIZE)) {
        error_setg(errp, "flash erase range is not sector-aligned");
        return false;
    }
    if (flash_offs > s->flash_size || count > s->flash_size - flash_offs) {
        error_setg(errp, "flash erase range is outside XIP storage");
        return false;
    }
    if (block_size && block_size != 64 * KiB) {
        g_autofree char *detail = g_strdup_printf("block size %" PRIu32,
                                                  block_size);

        rp2040_log_nyi("bootrom", "flash_range_erase block size", detail);
    }
    if (block_cmd != 0x20 && block_cmd != 0xd8) {
        g_autofree char *detail = g_strdup_printf("erase command 0x%02x",
                                                  block_cmd);

        rp2040_log_nyi("bootrom", "flash_range_erase command", detail);
    }

    memset(s->storage + flash_offs, 0xff, count);
    return rp2040_xip_writeback(s, errp);
}

bool rp2040_xip_flash_range_program(RP2040XipState *s, uint32_t flash_offs,
                                    uint32_t data_addr, uint32_t count,
                                    Error **errp)
{
    g_autofree uint8_t *buf = NULL;
    uint32_t i;

    if (!QEMU_IS_ALIGNED(flash_offs, FLASH_PAGE_SIZE) ||
        !QEMU_IS_ALIGNED(count, FLASH_PAGE_SIZE)) {
        error_setg(errp, "flash program range is not page-aligned");
        return false;
    }
    if (flash_offs > s->flash_size || count > s->flash_size - flash_offs) {
        error_setg(errp, "flash program range is outside XIP storage");
        return false;
    }

    buf = g_malloc(count);
    if (address_space_read(&address_space_memory, data_addr,
                           MEMTXATTRS_UNSPECIFIED, buf, count) != MEMTX_OK) {
        error_setg(errp, "could not read flash program buffer at 0x%08" PRIx32,
                   data_addr);
        return false;
    }

    for (i = 0; i < count; i++) {
        s->storage[flash_offs + i] &= buf[i];
    }

    return rp2040_xip_writeback(s, errp);
}

static void rp2040_xip_realize(DeviceState *dev, Error **errp)
{
    RP2040XipState *s = RP2040_XIP(dev);
    g_autofree gchar *contents = NULL;
    gsize contents_len = 0;

    if (s->flash_size == 0) {
        error_setg(errp, "flash-size must be non-zero");
        return;
    }

    s->xip_writable = true;
    s->storage = g_malloc0(s->flash_size);
    memset(s->storage, 0xff, s->flash_size);

    if (s->flash_file) {
        if (!g_file_get_contents(s->flash_file, &contents, &contents_len,
                                 NULL)) {
            error_setg(errp, "could not load flash file '%s'",
                       s->flash_file);
            return;
        }
        if (contents_len > s->flash_size) {
            error_setg(errp, "flash file '%s' is %" G_GSIZE_FORMAT
                       " bytes, larger than %" G_GSIZE_FORMAT
                       " byte Pico flash",
                       s->flash_file, contents_len, (gsize)s->flash_size);
            return;
        }
        memcpy(s->storage, contents, contents_len);
    }

    memory_region_init_io(&s->xip, OBJECT(dev), &rp2040_xip_ops, s,
                          "rp2040.xip", s->flash_size);
    memory_region_init_io(&s->xip_noalloc, OBJECT(dev),
                          &rp2040_xip_alias_ops, s, "rp2040.xip.noalloc",
                          s->flash_size);
    memory_region_init_io(&s->xip_nocache, OBJECT(dev),
                          &rp2040_xip_alias_ops, s, "rp2040.xip.nocache",
                          s->flash_size);
    memory_region_init_io(&s->xip_nocache_noalloc, OBJECT(dev),
                          &rp2040_xip_alias_ops, s,
                          "rp2040.xip.nocache-noalloc", s->flash_size);
    memory_region_init_io(&s->ctrl, OBJECT(dev), &rp2040_xip_ctrl_ops, s,
                          "rp2040.xip.ctrl", RP2040_XIP_CTRL_SIZE);
    memory_region_init_io(&s->ssi, OBJECT(dev), &rp2040_xip_ssi_ops, s,
                          "rp2040.xip.ssi", RP2040_XIP_SSI_SIZE);
    memory_region_init_io(&s->aux, OBJECT(dev), &rp2040_xip_aux_ops, s,
                          "rp2040.xip.aux", RP2040_XIP_AUX_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->xip);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->ctrl);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->ssi);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->aux);
}

static void rp2040_xip_reset(DeviceState *dev)
{
    RP2040XipState *s = RP2040_XIP(dev);

    s->xip_ctrl = RP2040_XIP_CTRL_EN | RP2040_XIP_CTRL_ERR_BADWRITE;
    s->ctrlr0 = 0;
    s->ctrlr1 = 0;
    s->ssienr = 0;
    s->ser = 0;
    s->baudr = 0;
    s->txftlr = 0;
    s->rxftlr = 0;
    s->imr = 0;
    s->dmacr = 0;
    s->dmatdlr = 0;
    s->dmardlr = 4;
    s->rx_sample_dly = 0;
    s->spi_ctrlr0 = 0;
    s->write_enable = false;
    s->busy = false;
    s->qspi_cs_high = true;
    s->stream_addr = 0;
    s->stream_ctr = 0;
    rp2040_xip_stream_clear(s);
    rp2040_xip_reset_tx(s);
    rp2040_xip_rx_clear(s);
}

static void rp2040_xip_finalize(Object *obj)
{
    RP2040XipState *s = RP2040_XIP(obj);

    g_free(s->flash_file);
    g_free(s->storage);
}

static void rp2040_xip_init(Object *obj)
{
    RP2040XipState *s = RP2040_XIP(obj);

    qdev_init_gpio_out_named(DEVICE(obj), &s->dreq_rx, "dreq-rx", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->dreq_stream, "dreq-stream", 1);
}

static const Property rp2040_xip_properties[] = {
    DEFINE_PROP_UINT32("flash-size", RP2040XipState, flash_size, 2 * MiB),
    DEFINE_PROP_STRING("flash-file", RP2040XipState, flash_file),
    DEFINE_PROP_UINT64("flash-uid", RP2040XipState, flash_uid,
                       FLASH_UID_DEFAULT),
};

static void rp2040_xip_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_xip_realize;
    device_class_set_legacy_reset(dc, rp2040_xip_reset);
    device_class_set_props(dc, rp2040_xip_properties);
}

static const TypeInfo rp2040_xip_info = {
    .name          = TYPE_RP2040_XIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040XipState),
    .instance_init = rp2040_xip_init,
    .instance_finalize = rp2040_xip_finalize,
    .class_init    = rp2040_xip_class_init,
};

static void rp2040_xip_register_types(void)
{
    type_register_static(&rp2040_xip_info);
}
type_init(rp2040_xip_register_types)
