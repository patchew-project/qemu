/*
 *  CFI parallel flash with AMD command set emulation
 *
 *  Copyright (c) 2005 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * For now, this code can emulate flashes of 1, 2 or 4 bytes width.
 * Supported commands/modes are:
 * - flash read
 * - flash write
 * - flash ID read
 * - sector erase
 * - chip erase
 * - unlock bypass command
 * - CFI queries
 *
 * It does not implement software data protection as found in many real chips
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/block/block.h"
#include "hw/block/flash.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "qemu/timer.h"
#include "sysemu/block-backend.h"
#include "qemu/host-utils.h"
#include "hw/sysbus.h"
#include "trace.h"

#define PFLASH_DEBUG false
#define DPRINTF(fmt, ...)                                  \
do {                                                       \
    if (PFLASH_DEBUG) {                                    \
        fprintf(stderr, "PFLASH: " fmt, ## __VA_ARGS__);   \
    }                                                      \
} while (0)

#define PFLASH_LAZY_ROMD_THRESHOLD 42

/*
 * The size of the cfi_table indirectly depends on this and the start of the
 * PRI table directly depends on it. 4 is the maximum size (and also what
 * seems common) without changing the PRT table address.
 */
#define PFLASH_MAX_ERASE_REGIONS 4

/* Special write cycles for CFI queries. */
#define WCYCLE_CFI 7
#define WCYCLE_AUTOSELECT_CFI 8

struct PFlashCFI02 {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    BlockBackend *blk;
    uint32_t uniform_nb_blocs;
    uint32_t uniform_sector_len;
    uint32_t total_sectors;
    uint32_t nb_blocs[PFLASH_MAX_ERASE_REGIONS];
    uint32_t sector_len[PFLASH_MAX_ERASE_REGIONS];
    uint64_t total_len;
    uint64_t interleave_multiplier;
    uint8_t mappings;
    uint8_t bank_width; /* Width of the QEMU device in bytes. */
    uint8_t device_width; /* Width of individual pflash chip. */
    uint8_t max_device_width; /* Maximum width of individual pflash chip. */
    uint8_t be;
    int device_shift; /* Amount to shift an offset to get a device address. */
    int wcycle; /* if 0, the flash is read normally */
    int bypass;
    int ro;
    uint8_t cmd;
    uint64_t status;
    /* FIXME: implement array device properties */
    uint16_t ident0;
    uint16_t ident1;
    uint16_t ident2;
    uint16_t ident3;
    uint16_t unlock_addr0;
    uint16_t unlock_addr1;
    uint8_t cfi_table[0x4D];
    QEMUTimer timer;
    /* The device replicates the flash memory across its memory space.  Emulate
     * that by having a container (.mem) filled with an array of aliases
     * (.mem_mappings) pointing to the flash memory (.orig_mem).
     */
    MemoryRegion mem;
    MemoryRegion *mem_mappings;    /* array; one per mapping */
    MemoryRegion orig_mem;
    int rom_mode;
    int read_counter; /* used for lazy switch-back to rom mode */
    int sectors_to_erase;
    uint64_t erase_time_remaining;
    unsigned long *sector_erase_map;
    char *name;
    void *storage;
};

/*
 * Toggle status bit DQ7.
 */
static inline void toggle_dq7(PFlashCFI02 *pfl)
{
    pfl->status ^= pfl->interleave_multiplier * 0x80;
}

/*
 * Set status bit DQ7 to bit 7 of value.
 */
static inline void set_dq7(PFlashCFI02 *pfl, uint64_t value)
{
    uint64_t mask = pfl->interleave_multiplier * 0x80;
    pfl->status &= ~mask;
    pfl->status |= value & mask;
}

/*
 * Toggle status bit DQ6.
 */
static inline void toggle_dq6(PFlashCFI02 *pfl)
{
    pfl->status ^= pfl->interleave_multiplier * 0x40;
}

/*
 * Turn on DQ3.
 */
static inline void assert_dq3(PFlashCFI02 *pfl)
{
    pfl->status |= pfl->interleave_multiplier * 0x08;
}

/*
 * Turn off DQ3.
 */
static inline void reset_dq3(PFlashCFI02 *pfl)
{
    pfl->status &= ~(pfl->interleave_multiplier * 0x08);
}

/*
 * Toggle status bit DQ2.
 */
static inline void toggle_dq2(PFlashCFI02 *pfl)
{
    pfl->status ^= pfl->interleave_multiplier * 0x04;
}

/*
 * Set up replicated mappings of the same region.
 */
static void pflash_setup_mappings(PFlashCFI02 *pfl)
{
    unsigned i;
    hwaddr size = memory_region_size(&pfl->orig_mem);

    memory_region_init(&pfl->mem, OBJECT(pfl), "pflash", pfl->mappings * size);
    pfl->mem_mappings = g_new(MemoryRegion, pfl->mappings);
    for (i = 0; i < pfl->mappings; ++i) {
        memory_region_init_alias(&pfl->mem_mappings[i], OBJECT(pfl),
                                 "pflash-alias", &pfl->orig_mem, 0, size);
        memory_region_add_subregion(&pfl->mem, i * size, &pfl->mem_mappings[i]);
    }
}

static void pflash_register_memory(PFlashCFI02 *pfl, int rom_mode)
{
    memory_region_rom_device_set_romd(&pfl->orig_mem, rom_mode);
    pfl->rom_mode = rom_mode;
}

/*
 * Returns the time it takes to erase the number of sectors scheduled for
 * erasure based on CFI address 0x21 which is "Typical timeout per individual
 * block erase 2^N ms."
 */
static uint64_t pflash_erase_time(PFlashCFI02 *pfl)
{
    /*
     * If there are no sectors to erase (which can happen if all of the sectors
     * to be erased are protected), then erase takes 100 us. Protected sectors
     * aren't supported so this should never happen.
     */
    return ((1ULL << pfl->cfi_table[0x21]) * pfl->sectors_to_erase) * SCALE_US;
}

/*
 * Returns true if the device is currently in erase suspend mode.
 */
static inline bool pflash_erase_suspend_mode(PFlashCFI02 *pfl)
{
    return pfl->erase_time_remaining > 0;
}

static void pflash_timer(void *opaque)
{
    PFlashCFI02 *pfl = opaque;

    trace_pflash_timer_expired(pfl->cmd);
    if (pfl->cmd == 0x30) {
        /*
         * Sector erase. If DQ3 is 0 when the timer expires, then the 50
         * us erase timeout has expired so we need to start the timer for the
         * sector erase algorithm. Otherwise, the erase completed and we should
         * go back to read array mode.
         */
        if ((pfl->status & 0x08) == 0) {
            assert_dq3(pfl);
            uint64_t timeout = pflash_erase_time(pfl);
            timer_mod(&pfl->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + timeout);
            DPRINTF("%s: erase timeout fired; erasing %d sectors\n",
                    __func__, pfl->sectors_to_erase);
            return;
        }
        DPRINTF("%s: sector erase complete\n", __func__);
        bitmap_zero(pfl->sector_erase_map, pfl->total_sectors);
        pfl->sectors_to_erase = 0;
        reset_dq3(pfl);
    }

    /* Reset flash */
    toggle_dq7(pfl);
    if (pfl->bypass) {
        pfl->wcycle = 2;
    } else {
        pflash_register_memory(pfl, 1);
        pfl->wcycle = 0;
    }
    pfl->cmd = 0;
}

/*
 * Read data from flash.
 */
static uint64_t pflash_data_read(PFlashCFI02 *pfl, hwaddr offset,
                                 unsigned int width)
{
    uint8_t *p = (uint8_t *)pfl->storage + offset;
    uint64_t ret = pfl->be? ldn_be_p(p, width) : ldn_le_p(p, width);
    /* XXX: Need a trace_pflash_data_read(offset, ret, width) */
    switch (width) {
    case 1:
        trace_pflash_data_read8(offset, ret);
        break;
    case 2:
        trace_pflash_data_read16(offset, ret);
        break;
    case 4:
        trace_pflash_data_read32(offset, ret);
        break;
    }
    return ret;
}

typedef struct {
    uint32_t len;
    uint32_t num;
} SectorInfo;

/*
 * offset should be a byte offset of the QEMU device and _not_ a device
 * offset.
 */
static SectorInfo pflash_sector_info(PFlashCFI02 *pfl, hwaddr offset)
{
    assert(offset < pfl->total_len);
    int nb_regions = pfl->cfi_table[0x2C];
    hwaddr addr = 0;
    uint32_t sector_num = 0;
    for (int i = 0; i < nb_regions; ++i) {
        uint64_t region_size = (uint64_t)pfl->nb_blocs[i] * pfl->sector_len[i];
        if (addr <= offset && offset < addr + region_size) {
            return (SectorInfo) {
                .len = pfl->sector_len[i],
                .num = sector_num + (offset - addr) / pfl->sector_len[i],
            };
        }
        sector_num += pfl->nb_blocs[i];
        addr += region_size;
    }
    abort();
}

/*
 * Returns true if the offset refers to a flash sector that is currently being
 * erased.
 */
static bool pflash_sector_is_erasing(PFlashCFI02 *pfl, hwaddr offset)
{
    long sector_num = pflash_sector_info(pfl, offset).num;
    return test_bit(sector_num, pfl->sector_erase_map);
}

static uint64_t pflash_read(void *opaque, hwaddr offset, unsigned int width)
{
    PFlashCFI02 *pfl = opaque;
    uint64_t ret;

    ret = -1;
    trace_pflash_read(offset, pfl->cmd, width, pfl->wcycle);
    /* Lazy reset to ROMD mode after a certain amount of read accesses */
    if (!pfl->rom_mode && pfl->wcycle == 0 &&
        ++pfl->read_counter > PFLASH_LAZY_ROMD_THRESHOLD) {
        pflash_register_memory(pfl, 1);
    }
    /* Mask by the total length of the chip to account for alias mappings. */
    offset &= pfl->total_len - 1;
    hwaddr device_addr = offset >> pfl->device_shift;

    switch (pfl->cmd) {
    default:
        /* This should never happen : reset state & treat it as a read*/
        DPRINTF("%s: unknown command state: %x\n", __func__, pfl->cmd);
        pfl->wcycle = 0;
        pfl->cmd = 0;
        /* fall through to the read code */
    case 0x80:
        /* We accept reads during second unlock sequence... */
    case 0x00:
        if (pflash_erase_suspend_mode(pfl) &&
            pflash_sector_is_erasing(pfl, offset)) {
            /* Toggle bit 2, but not 6. */
            toggle_dq2(pfl);
            /* Status register read */
            ret = pfl->status;
            DPRINTF("%s: status %" PRIx64 "\n", __func__, ret);
            break;
        }
        /* Flash area read */
        return pflash_data_read(pfl, offset, width);
    case 0x90:
        /* flash ID read */
        switch (device_addr & 0xFF) {
        case 0x00:
            ret = pfl->ident0;
            break;
        case 0x01:
            ret = pfl->ident1;
            break;
        case 0x02:
            ret = 0x00; /* Pretend all sectors are unprotected */
            break;
        case 0x0E:
        case 0x0F:
            ret = device_addr & 0x01 ? pfl->ident3 : pfl->ident2;
            if (ret != (uint8_t)-1) {
                break;
            }
            /* Fall through to data read. */
        default:
            return pflash_data_read(pfl, offset, width);
        }
        ret *= pfl->interleave_multiplier;
        DPRINTF("%s: ID " TARGET_FMT_plx " %" PRIx64 "\n",
                __func__, device_addr & 0xFF, ret);
        break;
    case 0x10:
    case 0x30:
        /* Toggle bit 2 during erase, but not program. */
        toggle_dq2(pfl);
    case 0xA0:
        /* Toggle bit 6 */
        toggle_dq6(pfl);
        /* Status register read */
        ret = pfl->status;
        DPRINTF("%s: status %" PRIx64 "\n", __func__, ret);
        break;
    case 0x98:
        /* CFI query mode */
        if (device_addr < sizeof(pfl->cfi_table)) {
            ret = pfl->interleave_multiplier * pfl->cfi_table[device_addr];
        } else {
            ret = 0;
        }
        break;
    }

    return ret;
}

/* update flash content on disk */
static void pflash_update(PFlashCFI02 *pfl, int offset, int size)
{
    int offset_end;
    if (pfl->blk) {
        offset_end = offset + size;
        /* widen to sector boundaries */
        offset = QEMU_ALIGN_DOWN(offset, BDRV_SECTOR_SIZE);
        offset_end = QEMU_ALIGN_UP(offset_end, BDRV_SECTOR_SIZE);
        blk_pwrite(pfl->blk, offset, pfl->storage + offset,
                   offset_end - offset, 0);
    }
}

static void pflash_sector_erase(PFlashCFI02 *pfl, hwaddr offset)
{
    SectorInfo sector_info = pflash_sector_info(pfl, offset);
    uint64_t sector_len = sector_info.len;
    offset &= ~(sector_len - 1);
    DPRINTF("%s: start sector erase at %0*" PRIx64 "-%0*" PRIx64 "\n",
            __func__, pfl->bank_width * 2, offset,
            pfl->bank_width * 2, offset + sector_len - 1);
    if (!pfl->ro) {
        uint8_t *p = pfl->storage;
        memset(p + offset, 0xFF, sector_len);
        pflash_update(pfl, offset, sector_len);
    }
    set_dq7(pfl, 0x00);
    ++pfl->sectors_to_erase;
    set_bit(sector_info.num, pfl->sector_erase_map);
    /* Set (or reset) the 50 us timer for additional erase commands.  */
    timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50000);
}

static void pflash_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned int width)
{
    PFlashCFI02 *pfl = opaque;
    uint8_t *p;
    uint8_t cmd;

    cmd = value;
    if (pfl->cmd != 0xA0) {
        if (value != pfl->interleave_multiplier * cmd) {
            DPRINTF("%s: cmd 0x%02x not sent to all devices: expected="
                    "0x%0*" PRIx64 " actual=0x%0*" PRIx64 "\n",
                    __func__, cmd,
                    pfl->bank_width * 2, pfl->interleave_multiplier * cmd,
                    pfl->bank_width * 2, value);
        }

        /* Reset does nothing during chip erase and sector erase. */
        if (cmd == 0xF0 && pfl->cmd != 0x10 && pfl->cmd != 0x30) {
#if 0
            DPRINTF("%s: flash reset asked (%02x %02x)\n",
                    __func__, pfl->cmd, cmd);
#endif
            if (pfl->wcycle == WCYCLE_AUTOSELECT_CFI) {
                /* Return to autoselect mode. */
                pfl->wcycle = 3;
                pfl->cmd = 0x90;
                return;
            }
            goto reset_flash;
        }
    }

    trace_pflash_write(offset, value, width, pfl->wcycle);

    /* Mask by the total length of the chip to account for alias mappings. */
    offset &= pfl->total_len - 1;

    DPRINTF("%s: offset " TARGET_FMT_plx " 0x%0*" PRIx64 "\n",
            __func__, offset, width * 2, value);

    hwaddr device_addr = (offset >> pfl->device_shift);
    /* Address bits A11 and greater are don't cares for most commands. */
    unsigned int masked_addr = device_addr & 0x7FF;

    switch (pfl->wcycle) {
    case 0:
        /* Set the device in I/O access mode if required */
        if (pfl->rom_mode)
            pflash_register_memory(pfl, 0);
        pfl->read_counter = 0;
        /* We're in read mode */
    check_unlock0:
        if (masked_addr == 0x55 && cmd == 0x98) {
            /* Enter CFI query mode */
            pfl->wcycle = WCYCLE_CFI;
            pfl->cmd = 0x98;
            return;
        }
        /* Handle erase resume in erase suspend mode, otherwise reset. */
        if (cmd == 0x30) {
            if (pflash_erase_suspend_mode(pfl)) {
                /* Resume the erase. */
                timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          pfl->erase_time_remaining);
                pfl->erase_time_remaining = 0;
                pfl->wcycle = 6;
                pfl->cmd = 0x30;
                set_dq7(pfl, 0x00);
                assert_dq3(pfl);
                return;
            }
            goto reset_flash;
        }
        /* Ignore erase suspend. */
        if (cmd == 0xB0) {
            return;
        }
        if (masked_addr != pfl->unlock_addr0 || cmd != 0xAA) {
            DPRINTF("%s: unlock0 failed %04x %02x %04x\n",
                    __func__, masked_addr, cmd, pfl->unlock_addr0);
            goto reset_flash;
        }
        DPRINTF("%s: unlock sequence started\n", __func__);
        break;
    case 1:
        /* We started an unlock sequence */
    check_unlock1:
        if (masked_addr != pfl->unlock_addr1 || cmd != 0x55) {
            DPRINTF("%s: unlock1 failed %03x %02x\n", __func__,
                    masked_addr, cmd);
            goto reset_flash;
        }
        DPRINTF("%s: unlock sequence done\n", __func__);
        break;
    case 2:
        /* We finished an unlock sequence */
        if (!pfl->bypass && masked_addr != pfl->unlock_addr0) {
            DPRINTF("%s: command failed %03x %02x\n", __func__,
                    masked_addr, cmd);
            goto reset_flash;
        }
        switch (cmd) {
        case 0x20:
            pfl->bypass = 1;
            goto do_bypass;
        case 0x80:
        case 0x90:
        case 0xA0:
            pfl->cmd = cmd;
            DPRINTF("%s: starting command %02x\n", __func__, cmd);
            break;
        default:
            DPRINTF("%s: unknown command %02x\n", __func__, cmd);
            goto reset_flash;
        }
        break;
    case 3:
        switch (pfl->cmd) {
        case 0x80:
            /* We need another unlock sequence */
            goto check_unlock0;
        case 0xA0:
            if (pflash_erase_suspend_mode(pfl) &&
                pflash_sector_is_erasing(pfl, offset)) {
                /* Ignore writes to erasing sectors. */
                if (pfl->bypass) {
                    goto do_bypass;
                }
                goto reset_flash;
            }
            trace_pflash_data_write(offset, value, width, 0);
            if (!pfl->ro) {
                p = (uint8_t *)pfl->storage + offset;
                if (pfl->be) {
                    uint64_t current = ldn_be_p(p, width);
                    stn_be_p(p, width, current & value);
                } else {
                    uint64_t current = ldn_le_p(p, width);
                    stn_le_p(p, width, current & value);
                }
                pflash_update(pfl, offset, width);
            }
            /*
             * While programming, status bit DQ7 should hold the opposite
             * value from how it was programmed.
             */
            set_dq7(pfl, ~value);
            /* Let's pretend write is immediate */
            if (pfl->bypass)
                goto do_bypass;
            goto reset_flash;
        case 0x90:
            if (pfl->bypass && cmd == 0x00) {
                /* Unlock bypass reset */
                goto reset_flash;
            }
            /*
             * We can enter CFI query mode from autoselect mode, but we must
             * return to autoselect mode after a reset.
             */
            if (masked_addr == 0x55 && cmd == 0x98) {
                /* Enter autoselect CFI query mode */
                pfl->wcycle = WCYCLE_AUTOSELECT_CFI;
                pfl->cmd = 0x98;
                return;
            }
            /* No break here */
        default:
            DPRINTF("%s: invalid write for command %02x\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
    case 4:
        switch (pfl->cmd) {
        case 0xA0:
            /* Ignore writes while flash data write is occurring */
            /* As we suppose write is immediate, this should never happen */
            return;
        case 0x80:
            goto check_unlock1;
        default:
            /* Should never happen */
            DPRINTF("%s: invalid command state %02x (wc 4)\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
        break;
    case 5:
        if (pflash_erase_suspend_mode(pfl)) {
            /* Erasing is not supported in erase suspend mode. */
            goto reset_flash;
        }
        switch (cmd) {
        case 0x10:
            if (masked_addr != pfl->unlock_addr0) {
                DPRINTF("%s: chip erase: invalid address " TARGET_FMT_plx "\n",
                        __func__, offset);
                goto reset_flash;
            }
            /* Chip erase */
            DPRINTF("%s: start chip erase\n", __func__);
            if (!pfl->ro) {
                memset(pfl->storage, 0xFF, pfl->total_len);
                pflash_update(pfl, 0, pfl->total_len);
            }
            set_dq7(pfl, 0x00);
            /* Wait the time specified at CFI address 0x22. */
            timer_mod(&pfl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      (1ULL << pfl->cfi_table[0x22]) * SCALE_MS);
            break;
        case 0x30:
            /* Sector erase */
            pflash_sector_erase(pfl, offset);
            break;
        default:
            DPRINTF("%s: invalid command %02x (wc 5)\n", __func__, cmd);
            goto reset_flash;
        }
        pfl->cmd = cmd;
        break;
    case 6:
        switch (pfl->cmd) {
        case 0x10:
            /* Ignore writes during chip erase */
            return;
        case 0x30:
            if (cmd == 0xB0) {
                /*
                 * If erase suspend happens during the erase timeout (so DQ3 is
                 * 0), then the device suspends erasing immediately. Set the
                 * remaining time to be the total time to erase. Otherwise,
                 * there is a maximum amount of time it can take to enter
                 * suspend mode. Let's ignore that and suspend immediately and
                 * set the remaining time to the actual time remaining on the
                 * timer.
                 */
                if ((pfl->status & 0x08) == 0) {
                    pfl->erase_time_remaining = pflash_erase_time(pfl);
                } else {
                    int64_t delta = timer_expire_time_ns(&pfl->timer) -
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                    /* Make sure we have a positive time remaining. */
                    pfl->erase_time_remaining = delta <= 0 ? 1 : delta;
                }
                reset_dq3(pfl);
                timer_del(&pfl->timer);
                pfl->wcycle = 0;
                pfl->cmd = 0;
                return;
            }
            /*
             * If DQ3 is 0, additional sector erase commands can be
             * written and anything else (other than an erase suspend) resets
             * the device.
             */
            if ((pfl->status & 0x08) == 0) {
                if (cmd == 0x30) {
                    pflash_sector_erase(pfl, offset);
                } else {
                    goto reset_flash;
                }
            }
            /* Ignore writes during the actual erase. */
            return;
        default:
            /* Should never happen */
            DPRINTF("%s: invalid command state %02x (wc 6)\n",
                    __func__, pfl->cmd);
            goto reset_flash;
        }
        break;
    case WCYCLE_CFI: /* Special value for CFI queries */
    case WCYCLE_AUTOSELECT_CFI:
        DPRINTF("%s: invalid write in CFI query mode\n", __func__);
        goto reset_flash;
    default:
        /* Should never happen */
        DPRINTF("%s: invalid write state (wc 7)\n",  __func__);
        goto reset_flash;
    }
    pfl->wcycle++;

    return;

    /* Reset flash */
 reset_flash:
    trace_pflash_reset();
    pfl->bypass = 0;
    pfl->wcycle = 0;
    pfl->cmd = 0;
    return;

 do_bypass:
    pfl->wcycle = 2;
    pfl->cmd = 0;
}

static const MemoryRegionOps pflash_cfi02_ops = {
    .read = pflash_read,
    .write = pflash_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pflash_cfi02_realize(DeviceState *dev, Error **errp)
{
    PFlashCFI02 *pfl = PFLASH_CFI02(dev);
    int ret;
    Error *local_err = NULL;

    if (pfl->uniform_sector_len == 0 && pfl->sector_len[0] == 0) {
        error_setg(errp, "attribute \"sector-length\" not specified or zero.");
        return;
    }
    if (pfl->uniform_nb_blocs == 0 && pfl->nb_blocs[0] == 0) {
        error_setg(errp, "attribute \"num-blocks\" not specified or zero.");
        return;
    }
    if (pfl->name == NULL) {
        error_setg(errp, "attribute \"name\" not specified.");
        return;
    }

    if (pfl->bank_width == 0) {
        error_setg(errp, "attribute \"width\" not specified or zero.");
        return;
    }

    /*
     * device-width defaults to width and max-device-width defaults to
     * device-width. Check that the device-width and max-device-width
     * configurations are supported.
     */
    if (pfl->device_width == 0) {
        pfl->device_width = pfl->bank_width;
    }
    if (pfl->max_device_width == 0) {
        pfl->max_device_width = pfl->device_width;
    }
    if (pfl->bank_width % pfl->device_width != 0) {
        error_setg(errp,
                   "attribute \"width\" (%u) not a multiple of attribute "
                   "\"device-width\" (%u).",
                   pfl->bank_width, pfl->device_width);
        return;
    }

    /*
     * Writing commands to the flash device and reading CFI responses or
     * status values requires transforming a QEMU device offset into a
     * flash device address given in terms of the device's maximum width. We
     * can do this by shifting a QEMU device offset right a constant number of
     * bits depending on the bank_width, device_width, and max_device_width.
     *
     * num_devices = bank_width / device_width is the number of interleaved
     * flash devices. To compute a device byte address, we need to divide
     * offset by num_devices (equivalently shift right by log2(num_devices)).
     * To turn a device byte address into a device word address, we need to
     * divide by max_device_width (equivalently shift right by
     * log2(max_device_width)).
     *
     * In tabular form.
     * ==================================================================
     * bank_width   device_width    max_device_width    num_devices shift
     * ------------------------------------------------------------------
     * 1            1               1                   1           0
     * 1            1               2                   1           1
     * 2            1               1                   2           1
     * 2            1               2                   2           2
     * 2            2               2                   1           1
     * 4            1               1                   4           2
     * 4            1               2                   4           3
     * 4            1               4                   4           4
     * 4            2               2                   2           2
     * 4            2               4                   2           3
     * 4            4               4                   1           2
     * ==================================================================
     */
    pfl->device_shift = ctz32(pfl->bank_width) - ctz32(pfl->device_width) +
                        ctz32(pfl->max_device_width);
    pfl->interleave_multiplier = 0;
    for (unsigned int shift = 0; shift < pfl->bank_width;
         shift += pfl->device_width) {
        pfl->interleave_multiplier |= 1 << (shift * 8);
    }

    uint16_t device_interface_code;
    if (pfl->max_device_width == 1 && pfl->device_width == 1) {
        device_interface_code = 0; /* x8 only. */
    } else if (pfl->max_device_width == 2 &&
               (pfl->device_width == 1 || pfl->device_width == 2)) {
        /* XXX: Some devices only support x16, this code doesn't model them. */
        device_interface_code = 2; /* Supports x8 or x16. */
    } else if (pfl->max_device_width == 4 && pfl->device_width == 1) {
        /*
         * XXX: this is x32-only. The standards I've seen don't specify a value
         * for x8/x32 but do mention them.
         */
        device_interface_code = 3; /* x32 only. */
    } else if (pfl->max_device_width == 4 &&
               (pfl->device_width == 2 || pfl->device_width == 4)) {
        device_interface_code = 4; /* Supports x16 or x32. */
    } else {
        error_setg(errp,
                   "unsupported configuration: \"device-width\"=%u "
                   "\"max-device-width\"=%u.",
                   pfl->device_width, pfl->max_device_width);
        return;
    }

    int num_devices = pfl->bank_width / pfl->device_width;
    int nb_regions;
    pfl->total_len = 0;
    pfl->total_sectors = 0;
    for (nb_regions = 0; nb_regions < PFLASH_MAX_ERASE_REGIONS; ++nb_regions) {
        if (pfl->nb_blocs[nb_regions] == 0) {
            break;
        }
        pfl->total_sectors += pfl->nb_blocs[nb_regions];
        uint64_t sector_len_per_device = pfl->sector_len[nb_regions] /
                                         num_devices;

        /*
         * The size of each flash sector must be a power of 2 and it must be
         * aligned at the same power of 2.
         */
        if (sector_len_per_device & 0xff ||
            sector_len_per_device >= (1 << 24) ||
            !is_power_of_2(sector_len_per_device))
        {
            error_setg(errp, "unsupported configuration: "
                       "sector length[%d] per device = %" PRIx64 ".",
                       nb_regions, sector_len_per_device);
            return;
        }
        if ((pfl->total_len / num_devices) & (sector_len_per_device - 1)) {
            error_setg(errp, "unsupported configuration: "
                       "flash region %d not correctly aligned.",
                       nb_regions);
            return;
        }

        pfl->total_len += (uint64_t)pfl->sector_len[nb_regions] *
                          pfl->nb_blocs[nb_regions];
    }

    uint64_t uniform_len = (uint64_t)pfl->uniform_nb_blocs *
                           pfl->uniform_sector_len;
    if (nb_regions == 0) {
        nb_regions = 1;
        pfl->nb_blocs[0] = pfl->uniform_nb_blocs;
        pfl->sector_len[0] = pfl->uniform_sector_len;
        pfl->total_len = uniform_len;
        pfl->total_sectors = pfl->uniform_nb_blocs;
    } else if (uniform_len != 0 && uniform_len != pfl->total_len) {
        error_setg(errp, "\"num-blocks\"*\"sector-length\" "
                   "different from \"num-blocks0\"*\'sector-length0\" + ... + "
                   "\"num-blocks3\"*\"sector-length3\"");
        return;
    }

    /*
     * If the flash is not a power of 2, then the code for handling multiple
     * mappings will not work correctly.
     */
    if (!is_power_of_2(pfl->total_len)) {
        error_setg(errp, "total pflash length (%" PRIx64 ") not a power of 2.",
                   pfl->total_len);
        return;
    }

    memory_region_init_rom_device(&pfl->orig_mem, OBJECT(pfl),
                                  &pflash_cfi02_ops, pfl, pfl->name,
                                  pfl->total_len, &local_err);
    /* Only 11 bits are used in the comparison. */
    pfl->unlock_addr0 &= 0x7FF;
    pfl->unlock_addr1 &= 0x7FF;

    /* Allocate memory for a bitmap for sectors being erased. */
    pfl->sector_erase_map = bitmap_new(pfl->total_sectors);

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pfl->storage = memory_region_get_ram_ptr(&pfl->orig_mem);

    if (pfl->blk) {
        uint64_t perm;
        pfl->ro = blk_is_read_only(pfl->blk);
        perm = BLK_PERM_CONSISTENT_READ | (pfl->ro ? 0 : BLK_PERM_WRITE);
        ret = blk_set_perm(pfl->blk, perm, BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }
    } else {
        pfl->ro = 0;
    }

    if (pfl->blk) {
        if (!blk_check_size_and_read_all(pfl->blk, pfl->storage,
                                         pfl->total_len, errp)) {
            vmstate_unregister_ram(&pfl->orig_mem, DEVICE(pfl));
            return;
        }
    }

    pflash_setup_mappings(pfl);
    pfl->rom_mode = 1;
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &pfl->mem);

    timer_init_ns(&pfl->timer, QEMU_CLOCK_VIRTUAL, pflash_timer, pfl);
    pfl->wcycle = 0;
    pfl->cmd = 0;
    pfl->status = 0;

    /* Hardcoded CFI table (mostly from SG29 Spansion flash) */
    /* Standard "QRY" string */
    pfl->cfi_table[0x10] = 'Q';
    pfl->cfi_table[0x11] = 'R';
    pfl->cfi_table[0x12] = 'Y';
    /* Command set (AMD/Fujitsu) */
    pfl->cfi_table[0x13] = 0x02;
    pfl->cfi_table[0x14] = 0x00;
    /* Primary extended table address */
    pfl->cfi_table[0x15] = 0x40;
    pfl->cfi_table[0x16] = 0x00;
    /* Alternate command set (none) */
    pfl->cfi_table[0x17] = 0x00;
    pfl->cfi_table[0x18] = 0x00;
    /* Alternate extended table (none) */
    pfl->cfi_table[0x19] = 0x00;
    pfl->cfi_table[0x1A] = 0x00;
    /* Vcc min */
    pfl->cfi_table[0x1B] = 0x27;
    /* Vcc max */
    pfl->cfi_table[0x1C] = 0x36;
    /* Vpp min (no Vpp pin) */
    pfl->cfi_table[0x1D] = 0x00;
    /* Vpp max (no Vpp pin) */
    pfl->cfi_table[0x1E] = 0x00;
    /* Timeout per single byte/word write (16 us) */
    pfl->cfi_table[0x1F] = 0x04;
    /* Timeout for min size buffer write (NA) */
    pfl->cfi_table[0x20] = 0x00;
    /* Typical timeout for block erase (512 ms) */
    pfl->cfi_table[0x21] = 0x09;
    /* Typical timeout for full chip erase (4096 ms) */
    pfl->cfi_table[0x22] = 0x0C;
    /* Reserved */
    pfl->cfi_table[0x23] = 0x01;
    /* Max timeout for buffer write (NA) */
    pfl->cfi_table[0x24] = 0x00;
    /* Max timeout for block erase */
    pfl->cfi_table[0x25] = 0x0A;
    /* Max timeout for chip erase */
    pfl->cfi_table[0x26] = 0x0D;
    /* Device size */
    pfl->cfi_table[0x27] = ctz32(pfl->total_len / num_devices);
    /* Flash device interface  */
    pfl->cfi_table[0x28] = device_interface_code;
    pfl->cfi_table[0x29] = device_interface_code >> 8;
    /* Max number of bytes in multi-bytes write */
    /* XXX: disable buffered write as it's not supported */
    /*    pfl->cfi_table[0x2A] = 0x05; */
    pfl->cfi_table[0x2A] = 0x00;
    pfl->cfi_table[0x2B] = 0x00;
    /* Number of erase block regions */
    pfl->cfi_table[0x2C] = nb_regions;
    /* Erase block regions */
    for (int i = 0; i < nb_regions; ++i) {
        uint32_t sector_len_per_device = pfl->sector_len[i] / num_devices;
        pfl->cfi_table[0x2D + 4 * i] = pfl->nb_blocs[i] - 1;
        pfl->cfi_table[0x2E + 4 * i] = (pfl->nb_blocs[i] - 1) >> 8;
        pfl->cfi_table[0x2F + 4 * i] = sector_len_per_device >> 8;
        pfl->cfi_table[0x30 + 4 * i] = sector_len_per_device >> 16;
    }

    /* Extended */
    pfl->cfi_table[0x40] = 'P';
    pfl->cfi_table[0x41] = 'R';
    pfl->cfi_table[0x42] = 'I';

    pfl->cfi_table[0x43] = '1'; /* version 1.0 */
    pfl->cfi_table[0x44] = '0';

    pfl->cfi_table[0x45] = 0x00; /* Address sensitive unlock required. */
    pfl->cfi_table[0x46] = 0x02; /* Erase suspend to read/write. */
    pfl->cfi_table[0x47] = 0x00; /* Sector protect not supported. */
    pfl->cfi_table[0x48] = 0x00; /* Temporary sector unprotect not supported. */

    pfl->cfi_table[0x49] = 0x00; /* Sector protect/unprotect scheme. */

    pfl->cfi_table[0x4a] = 0x00; /* Simultaneous operation not supported. */
    pfl->cfi_table[0x4b] = 0x00; /* Burst mode not supported. */
    pfl->cfi_table[0x4c] = 0x00; /* Page mode not supported. */
}

static Property pflash_cfi02_properties[] = {
    DEFINE_PROP_DRIVE("drive", PFlashCFI02, blk),
    DEFINE_PROP_UINT32("num-blocks", PFlashCFI02, uniform_nb_blocs, 0),
    DEFINE_PROP_UINT32("sector-length", PFlashCFI02, uniform_sector_len, 0),
    DEFINE_PROP_UINT32("num-blocks0", PFlashCFI02, nb_blocs[0], 0),
    DEFINE_PROP_UINT32("sector-length0", PFlashCFI02, sector_len[0], 0),
    DEFINE_PROP_UINT32("num-blocks1", PFlashCFI02, nb_blocs[1], 0),
    DEFINE_PROP_UINT32("sector-length1", PFlashCFI02, sector_len[1], 0),
    DEFINE_PROP_UINT32("num-blocks2", PFlashCFI02, nb_blocs[2], 0),
    DEFINE_PROP_UINT32("sector-length2", PFlashCFI02, sector_len[2], 0),
    DEFINE_PROP_UINT32("num-blocks3", PFlashCFI02, nb_blocs[3], 0),
    DEFINE_PROP_UINT32("sector-length3", PFlashCFI02, sector_len[3], 0),
    DEFINE_PROP_UINT8("width", PFlashCFI02, bank_width, 0),
    DEFINE_PROP_UINT8("device-width", PFlashCFI02, device_width, 0),
    DEFINE_PROP_UINT8("max-device-width", PFlashCFI02, max_device_width, 0),
    DEFINE_PROP_UINT8("mappings", PFlashCFI02, mappings, 0),
    DEFINE_PROP_UINT8("big-endian", PFlashCFI02, be, 0),
    DEFINE_PROP_UINT16("id0", PFlashCFI02, ident0, 0),
    DEFINE_PROP_UINT16("id1", PFlashCFI02, ident1, 0),
    DEFINE_PROP_UINT16("id2", PFlashCFI02, ident2, 0),
    DEFINE_PROP_UINT16("id3", PFlashCFI02, ident3, 0),
    DEFINE_PROP_UINT16("unlock-addr0", PFlashCFI02, unlock_addr0, 0),
    DEFINE_PROP_UINT16("unlock-addr1", PFlashCFI02, unlock_addr1, 0),
    DEFINE_PROP_STRING("name", PFlashCFI02, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void pflash_cfi02_unrealize(DeviceState *dev, Error **errp)
{
    PFlashCFI02 *pfl = PFLASH_CFI02(dev);
    timer_del(&pfl->timer);
    g_free(pfl->sector_erase_map);
}

static void pflash_cfi02_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pflash_cfi02_realize;
    dc->unrealize = pflash_cfi02_unrealize;
    dc->props = pflash_cfi02_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pflash_cfi02_info = {
    .name           = TYPE_PFLASH_CFI02,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(PFlashCFI02),
    .class_init     = pflash_cfi02_class_init,
};

static void pflash_cfi02_register_types(void)
{
    type_register_static(&pflash_cfi02_info);
}

type_init(pflash_cfi02_register_types)

PFlashCFI02 *pflash_cfi02_register(hwaddr base,
                                   const char *name,
                                   hwaddr size,
                                   BlockBackend *blk,
                                   uint32_t sector_len,
                                   int nb_mappings, int bank_width,
                                   uint16_t id0, uint16_t id1,
                                   uint16_t id2, uint16_t id3,
                                   uint16_t unlock_addr0,
                                   uint16_t unlock_addr1,
                                   int be)
{
    DeviceState *dev = qdev_create(NULL, TYPE_PFLASH_CFI02);

    if (blk) {
        qdev_prop_set_drive(dev, "drive", blk, &error_abort);
    }
    assert(size % sector_len == 0);
    qdev_prop_set_uint32(dev, "num-blocks", size / sector_len);
    qdev_prop_set_uint32(dev, "sector-length", sector_len);
    qdev_prop_set_uint8(dev, "width", bank_width);
    qdev_prop_set_uint8(dev, "mappings", nb_mappings);
    qdev_prop_set_uint8(dev, "big-endian", !!be);
    qdev_prop_set_uint16(dev, "id0", id0);
    qdev_prop_set_uint16(dev, "id1", id1);
    qdev_prop_set_uint16(dev, "id2", id2);
    qdev_prop_set_uint16(dev, "id3", id3);
    qdev_prop_set_uint16(dev, "unlock-addr0", unlock_addr0);
    qdev_prop_set_uint16(dev, "unlock-addr1", unlock_addr1);
    qdev_prop_set_string(dev, "name", name);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    return PFLASH_CFI02(dev);
}
