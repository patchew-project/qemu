/*
 * QEMU PowerPC PowerNV Homer and OCC common area region
 *
 * Copyright (c) 2019, IBM Corporation.
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
#include "sysemu/python_api.h"
#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "sysemu/hw_accel.h"
#include "sysemu/cpus.h"
#include "hw/ppc/pnv.h"

static bool core_max_array(hwaddr addr)
{
    char *cpu_type;
    hwaddr core_max_base = 0xe2819;
    MachineState *ms = MACHINE(qdev_get_machine());
    cpu_type = strstr(ms->cpu_type, "power8");
    if (cpu_type)
        core_max_base = 0x1f8810;
    for (int i = 0; i <= ms->smp.cores; i++)
       if (addr == (core_max_base + i))
           return true;
    return false;
}

static uint64_t homer_read(void *opaque, hwaddr addr, unsigned width)
{
    if (homer_module && homer) {
        uint64_t homer_ret;
        char **address = g_malloc(sizeof(uint64_t));
        python_args_init_cast_long(address, addr, 0);
        homer_ret = python_callback_int(module_path, homer_module, homer, address, 1);
        python_args_clean(address, 1);
        g_free(address);
        return homer_ret;
    }
    switch (addr) {
        case 0xe2006:  /* max pstate ultra turbo */
        case 0xe2018:  /* pstate id for 0 */
        case 0x1f8001: /* P8 occ pstate version */
        case 0x1f8003: /* P8 pstate min */
        case 0x1f8010: /* P8 pstate id for 0 */
            return 0;
        case 0xe2000:  /* occ data area */
        case 0xe2002:  /* occ_role master/slave*/
        case 0xe2004:  /* pstate nom */
        case 0xe2005:  /* pstate turbo */
        case 0xe2020:  /* pstate id for 1 */
        case 0xe2818:  /* pstate ultra turbo */
        case 0xe2b85:  /* opal dynamic data (runtime) */
        case 0x1f8000: /* P8 occ pstate valid */
        case 0x1f8002: /* P8 throttle */
        case 0x1f8004: /* P8 pstate nom */
        case 0x1f8005: /* P8 pstate turbo */
        case 0x1f8012: /* vdd voltage identifier */
        case 0x1f8013: /* vcs voltage identifier */
        case 0x1f8018: /* P8 pstate id for 1 */
            return 1;
        case 0xe2003:  /* pstate min (2 as pstate min) */
        case 0xe2028:  /* pstate id for 2 */
        case 0x1f8006: /* P8 pstate ultra turbo */
        case 0x1f8020: /* P8 pstate id for 2 */
            return 2;
        case 0xe2001:  /* major version */
            return 0x90;
        /* 3000 khz frequency for 0, 1, and 2 pstates */
        case 0xe201c:
        case 0xe2024:
        case 0xe202c:
        /* P8 frequency for 0, 1, and 2 pstates */
        case 0x1f8014:
        case 0x1f801c:
        case 0x1f8024:
            return 3000;
        case 0x0:      /* homer base */
        case 0xe2008:  /* occ data area + 8 */
        case 0x1f8008: /* P8 occ data area + 8 */
        case 0x200008: /* homer base access to get homer image pointer*/
            return 0x1000000000000000;
    }
    /* pstate table core max array */
    if (core_max_array(addr))
        return 1;
    return 0;
}

static void homer_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned width)
{
    /* callback function defined to homer write */
    return;
}

const MemoryRegionOps pnv_homer_ops = {
    .read = homer_read,
    .write = homer_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t occ_common_area_read(void *opaque, hwaddr addr, unsigned width)
{
    if (occ_module && occ) {
        uint64_t occ_ret;
        char **address = g_malloc(sizeof(uint64_t));
        python_args_init_cast_long(address, addr, 0);
        occ_ret = python_callback_int(module_path, occ_module, occ, address, 1);
        python_args_clean(address, 1);
        g_free(address);
        return occ_ret;
    }
    switch (addr) {
        /*
         * occ-sensor sanity check that asserts the sensor
         * header block
         */
        case 0x580000: /* occ sensor data block */
        case 0x580001: /* valid */
        case 0x580002: /* version */
        case 0x580004: /* reading_version */
        case 0x580008: /* nr_sensors */
        case 0x580010: /* names_offset */
        case 0x580014: /* reading_ping_offset */
        case 0x58000c: /* reading_pong_offset */
        case 0x580023: /* structure_type */
            return 1;
        case 0x58000d: /* name length */
            return 0x30;
        case 0x580022: /* occ sensor loc core */
            return 0x0040;
        case 0x580003: /* occ sensor type power */
            return 0x0080;
        case 0x580005: /* sensor name */
            return 0x1000;
        case 0x58001e: /* HWMON_SENSORS_MASK */
        case 0x580020:
            return 0x8e00;
        case 0x0:      /* P8 slw base access for slw image size */
            return 0x1000000000000000;
    }
    return 0;
}

static void occ_common_area_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned width)
{
    /* callback function defined to occ common area write */
    return;
}

const MemoryRegionOps pnv_occ_common_area_ops = {
    .read = occ_common_area_read,
    .write = occ_common_area_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

void pnv_occ_common_area_realize(PnvChip *chip, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(chip);
    sbd->num_mmio = PNV_OCC_COMMON_AREA_SYSBUS;
    char *occ_common_area;

    /* occ common area */
    occ_common_area = g_strdup_printf("occ-common-area-%x", chip->chip_id);
    memory_region_init_io(&chip->occ_common_area_mmio, OBJECT(chip),
                          &pnv_occ_common_area_ops, chip, occ_common_area,
                          PNV_OCC_COMMON_AREA_SIZE);
    sysbus_init_mmio(sbd, &chip->occ_common_area_mmio);
    g_free(occ_common_area);
}

void pnv_homer_realize(PnvChip *chip, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(chip);
    sbd->num_mmio = PNV_HOMER_SYSBUS;
    char *homer;

    /* homer region */
    homer = g_strdup_printf("homer-%x", chip->chip_id);
    memory_region_init_io(&chip->homer_mmio, OBJECT(chip), &pnv_homer_ops,
                          chip, homer, PNV_HOMER_SIZE);
    sysbus_init_mmio(sbd, &chip->homer_mmio);
    g_free(homer);
}
