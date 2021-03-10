/*
 * Andes RISC-V AE350 Board
 *
 * Copyright (c) 2021 Andes Tech. Corp.
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

#ifndef HW_RISCV_ANDES_AE350_H
#define HW_RISCV_ANDES_AE350_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "qom/object.h"

#define ANDES_CPUS_MAX 4

#define TYPE_ANDES_AE350_SOC "riscv.andes.ae350.soc"
#define ANDES_AE350_SOC(obj) \
    OBJECT_CHECK(AndesAe350SocState, (obj), TYPE_ANDES_AE350_SOC)

typedef struct AndesAe350SocState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    DeviceState *plic_sw;
} AndesAe350SocState;

#define TYPE_ANDES_AE350_MACHINE MACHINE_TYPE_NAME("andes_ae350")
#define ANDES_AE350_MACHINE(obj) \
    OBJECT_CHECK(AndesAe350BoardState, (obj), TYPE_ANDES_AE350_MACHINE)

typedef struct AndesAe350BoardState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    AndesAe350SocState soc;
    void *fdt;
    int fdt_size;
} AndesAe350BoardState;

enum {
    ANDES_AE350_DEBUG,
    ANDES_AE350_MROM,
    ANDES_AE350_PLMT,
    ANDES_AE350_PLICSW,
    ANDES_AE350_PLIC,
    ANDES_AE350_UART1,
    ANDES_AE350_UART2,
    ANDES_AE350_DRAM,
    ANDES_AE350_GEM,
    ANDES_AE350_PIT,
    ANDES_AE350_SDC,
    ANDES_AE350_MAC,
    ANDES_AE350_VIRTIO,
};

enum {
    ANDES_AE350_PIT_IRQ = 3,
    ANDES_AE350_UART1_IRQ = 8,
    ANDES_AE350_UART2_IRQ = 9,
    ANDES_AE350_SDC_IRQ = 18,
    ANDES_AE350_MAC_IRQ = 19,
    ANDES_AE350_GEM_IRQ = 0x35,
    ANDES_AE350_VIRTIO_COUNT = 8,
    ANDES_AE350_VIRTIO_IRQ = 16, /* 16 to 23 */
};

#define ANDES_UART_REG_SHIFT    0x2
#define ANDES_UART_REG_OFFSET   0x20

#if defined(TARGET_RISCV32)
#define VIRT_CPU TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
#define VIRT_CPU TYPE_RISCV_CPU_BASE64
#endif

#endif /* HW_RISCV_ANDES_AE350_H */
