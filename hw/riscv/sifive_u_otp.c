/*
 * QEMU SiFive U OTP (One-Time Programmable) Memory interface
 *
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * Simple model of the OTP to emulate register reads made by the SDK BSP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/riscv/sifive_u_otp.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>

#define TRACE_PREFIX            "FU540_OTP: "
#define SIFIVE_FU540_OTP_SIZE   (SIFIVE_U_OTP_NUM_FUSES * 4)

static int otp_backed_fd;
static unsigned int *otp_mmap;

static void sifive_u_otp_backed_load(const char *filename);
static uint64_t sifive_u_otp_backed_read(uint32_t fuseidx);
static void sifive_u_otp_backed_write(uint32_t fuseidx,
                                      uint32_t paio,
                                      uint32_t pdin);
static void sifive_u_otp_backed_unload(void);

void sifive_u_otp_backed_load(const char *filename)
{
    if (otp_backed_fd < 0) {

        otp_backed_fd = open(filename, O_RDWR);

        if (otp_backed_fd < 0)
            qemu_log_mask(LOG_TRACE,
                          TRACE_PREFIX "Warning: can't open otp file\n");
        else {

            otp_mmap = (unsigned int *)mmap(0,
                                            SIFIVE_FU540_OTP_SIZE,
                                            PROT_READ | PROT_WRITE | PROT_EXEC,
                                            MAP_FILE | MAP_SHARED,
                                            otp_backed_fd,
                                            0);

            if (otp_mmap == MAP_FAILED)
                qemu_log_mask(LOG_TRACE,
                              TRACE_PREFIX "Warning: can't mmap otp file\n");
        }
    }

}

uint64_t sifive_u_otp_backed_read(uint32_t fuseidx)
{
    return (uint64_t)(otp_mmap[fuseidx]);
}

void sifive_u_otp_backed_write(uint32_t fuseidx, uint32_t paio, uint32_t pdin)
{
    otp_mmap[fuseidx] &= ~(pdin << paio);
    otp_mmap[fuseidx] |= (pdin << paio);
}


void sifive_u_otp_backed_unload(void)
{
    munmap(otp_mmap, SIFIVE_FU540_OTP_SIZE);
    close(otp_backed_fd);
    otp_backed_fd = -1;
}

static uint64_t sifive_u_otp_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveUOTPState *s = opaque;

    switch (addr) {
    case SIFIVE_U_OTP_PA:
        return s->pa;
    case SIFIVE_U_OTP_PAIO:
        return s->paio;
    case SIFIVE_U_OTP_PAS:
        return s->pas;
    case SIFIVE_U_OTP_PCE:
        return s->pce;
    case SIFIVE_U_OTP_PCLK:
        return s->pclk;
    case SIFIVE_U_OTP_PDIN:
        return s->pdin;
    case SIFIVE_U_OTP_PDOUT:
        if ((s->pce & SIFIVE_U_OTP_PCE_EN) &&
            (s->pdstb & SIFIVE_U_OTP_PDSTB_EN) &&
            (s->ptrim & SIFIVE_U_OTP_PTRIM_EN)) {

            if (otp_file) {
                uint64_t val;

                sifive_u_otp_backed_load(otp_file);
                val = sifive_u_otp_backed_read(s->pa);
                sifive_u_otp_backed_unload();

                return val;
            } else
                return s->fuse[s->pa & SIFIVE_U_OTP_PA_MASK];
        } else {
            return 0xff;
        }
    case SIFIVE_U_OTP_PDSTB:
        return s->pdstb;
    case SIFIVE_U_OTP_PPROG:
        return s->pprog;
    case SIFIVE_U_OTP_PTC:
        return s->ptc;
    case SIFIVE_U_OTP_PTM:
        return s->ptm;
    case SIFIVE_U_OTP_PTM_REP:
        return s->ptm_rep;
    case SIFIVE_U_OTP_PTR:
        return s->ptr;
    case SIFIVE_U_OTP_PTRIM:
        return s->ptrim;
    case SIFIVE_U_OTP_PWE:
        return s->pwe;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void sifive_u_otp_write(void *opaque, hwaddr addr,
                               uint64_t val64, unsigned int size)
{
    SiFiveUOTPState *s = opaque;
    uint32_t val32 = (uint32_t)val64;

    switch (addr) {
    case SIFIVE_U_OTP_PA:
        s->pa = val32 & SIFIVE_U_OTP_PA_MASK;
        break;
    case SIFIVE_U_OTP_PAIO:
        s->paio = val32;
        break;
    case SIFIVE_U_OTP_PAS:
        s->pas = val32;
        break;
    case SIFIVE_U_OTP_PCE:
        s->pce = val32;
        break;
    case SIFIVE_U_OTP_PCLK:
        s->pclk = val32;
        break;
    case SIFIVE_U_OTP_PDIN:
        s->pdin = val32;
        break;
    case SIFIVE_U_OTP_PDOUT:
        /* read-only */
        break;
    case SIFIVE_U_OTP_PDSTB:
        s->pdstb = val32;
        break;
    case SIFIVE_U_OTP_PPROG:
        s->pprog = val32;
        break;
    case SIFIVE_U_OTP_PTC:
        s->ptc = val32;
        break;
    case SIFIVE_U_OTP_PTM:
        s->ptm = val32;
        break;
    case SIFIVE_U_OTP_PTM_REP:
        s->ptm_rep = val32;
        break;
    case SIFIVE_U_OTP_PTR:
        s->ptr = val32;
        break;
    case SIFIVE_U_OTP_PTRIM:
        s->ptrim = val32;
        break;
    case SIFIVE_U_OTP_PWE:
        if (otp_file) {
            sifive_u_otp_backed_load(otp_file);
            sifive_u_otp_backed_write(s->pa, s->paio, s->pdin);
            sifive_u_otp_backed_unload();
        }

        s->pwe = val32;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%" HWADDR_PRIx
                      " v=0x%x\n", __func__, addr, val32);
    }
}

static const MemoryRegionOps sifive_u_otp_ops = {
    .read = sifive_u_otp_read,
    .write = sifive_u_otp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property sifive_u_otp_properties[] = {
    DEFINE_PROP_UINT32("serial", SiFiveUOTPState, serial, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sifive_u_otp_realize(DeviceState *dev, Error **errp)
{
    SiFiveUOTPState *s = SIFIVE_U_OTP(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &sifive_u_otp_ops, s,
                          TYPE_SIFIVE_U_OTP, SIFIVE_U_OTP_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void sifive_u_otp_reset(DeviceState *dev)
{
    SiFiveUOTPState *s = SIFIVE_U_OTP(dev);

    /* Initialize all fuses' initial value to 0xFFs */
    memset(s->fuse, 0xff, sizeof(s->fuse));

    /* Make a valid content of serial number */
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR] = s->serial;
    s->fuse[SIFIVE_U_OTP_SERIAL_ADDR + 1] = ~(s->serial);

    /* Initialize file mmap and descriptor. */
    otp_mmap = NULL;
    otp_backed_fd = -1;
}

static void sifive_u_otp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sifive_u_otp_properties);
    dc->realize = sifive_u_otp_realize;
    dc->reset = sifive_u_otp_reset;
}

static const TypeInfo sifive_u_otp_info = {
    .name          = TYPE_SIFIVE_U_OTP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveUOTPState),
    .class_init    = sifive_u_otp_class_init,
};

static void sifive_u_otp_register_types(void)
{
    type_register_static(&sifive_u_otp_info);
}

type_init(sifive_u_otp_register_types)
