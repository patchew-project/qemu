/*
 * Nuclei U series  SOC machine interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_RISCV_NUCLEI_HBIRD_H
#define HW_RISCV_NUCLEI_HBIRD_H

#include "hw/char/nuclei_uart.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"

#define TYPE_NUCLEI_HBIRD_SOC "riscv.nuclei.hbird.soc"
#define RISCV_NUCLEI_HBIRD_SOC(obj) \
    OBJECT_CHECK(NucleiHBSoCState, (obj), TYPE_NUCLEI_HBIRD_SOC)

typedef struct NucleiHBSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;

    DeviceState *eclic;
    MemoryRegion ilm;
    MemoryRegion dlm;
    MemoryRegion internal_rom;
    MemoryRegion xip_mem;

    DeviceState *timer;
    NucLeiUARTState uart;
    SIFIVEGPIOState gpio;

} NucleiHBSoCState;

#define TYPE_HBIRD_FPGA_MACHINE MACHINE_TYPE_NAME("hbird_fpga")
#define HBIRD_FPGA_MACHINE(obj) \
    OBJECT_CHECK(NucleiHBState, (obj), TYPE_HBIRD_FPGA_MACHINE)

typedef struct NucleiHBState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    NucleiHBSoCState soc;

    uint32_t msel;
} NucleiHBState;

enum {
    MSEL_ILM = 1,
    MSEL_FLASH = 2,
    MSEL_FLASHXIP = 3,
    MSEL_DDR = 4
};

enum {
    HBIRD_DEBUG,
    HBIRD_ROM,
    HBIRD_TIMER,
    HBIRD_ECLIC,
    HBIRD_GPIO,
    HBIRD_UART0,
    HBIRD_QSPI0,
    HBIRD_PWM0,
    HBIRD_UART1,
    HBIRD_QSPI1,
    HBIRD_PWM1,
    HBIRD_QSPI2,
    HBIRD_PWM2,
    HBIRD_XIP,
    HBIRD_DRAM,
    HBIRD_ILM,
    HBIRD_DLM
};

enum {
    HBIRD_SOC_INT19_IRQn = 19, /*!< Device Interrupt */
    HBIRD_SOC_INT20_IRQn = 20, /*!< Device Interrupt */
    HBIRD_SOC_INT21_IRQn = 21, /*!< Device Interrupt */
    HBIRD_SOC_INT22_IRQn = 22, /*!< Device Interrupt */
    HBIRD_SOC_INT23_IRQn = 23, /*!< Device Interrupt */
    HBIRD_SOC_INT24_IRQn = 24, /*!< Device Interrupt */
    HBIRD_SOC_INT25_IRQn = 25, /*!< Device Interrupt */
    HBIRD_SOC_INT26_IRQn = 26, /*!< Device Interrupt */
    HBIRD_SOC_INT27_IRQn = 27, /*!< Device Interrupt */
    HBIRD_SOC_INT28_IRQn = 28, /*!< Device Interrupt */
    HBIRD_SOC_INT29_IRQn = 29, /*!< Device Interrupt */
    HBIRD_SOC_INT30_IRQn = 30, /*!< Device Interrupt */
    HBIRD_SOC_INT31_IRQn = 31, /*!< Device Interrupt */
    HBIRD_SOC_INT32_IRQn = 32, /*!< Device Interrupt */
    HBIRD_SOC_INT33_IRQn = 33, /*!< Device Interrupt */
    HBIRD_SOC_INT34_IRQn = 34, /*!< Device Interrupt */
    HBIRD_SOC_INT35_IRQn = 35, /*!< Device Interrupt */
    HBIRD_SOC_INT36_IRQn = 36, /*!< Device Interrupt */
    HBIRD_SOC_INT37_IRQn = 37, /*!< Device Interrupt */
    HBIRD_SOC_INT38_IRQn = 38, /*!< Device Interrupt */
    HBIRD_SOC_INT39_IRQn = 39, /*!< Device Interrupt */
    HBIRD_SOC_INT40_IRQn = 40, /*!< Device Interrupt */
    HBIRD_SOC_INT41_IRQn = 41, /*!< Device Interrupt */
    HBIRD_SOC_INT42_IRQn = 42, /*!< Device Interrupt */
    HBIRD_SOC_INT43_IRQn = 43, /*!< Device Interrupt */
    HBIRD_SOC_INT44_IRQn = 44, /*!< Device Interrupt */
    HBIRD_SOC_INT45_IRQn = 45, /*!< Device Interrupt */
    HBIRD_SOC_INT46_IRQn = 46, /*!< Device Interrupt */
    HBIRD_SOC_INT47_IRQn = 47, /*!< Device Interrupt */
    HBIRD_SOC_INT48_IRQn = 48, /*!< Device Interrupt */
    HBIRD_SOC_INT49_IRQn = 49, /*!< Device Interrupt */
    HBIRD_SOC_INT50_IRQn = 50, /*!< Device Interrupt */
    HBIRD_SOC_INT_MAX,
};

#if defined(TARGET_RISCV32)
#define NUCLEI_N_CPU TYPE_RISCV_CPU_NUCLEI_N307FD
#elif defined(TARGET_RISCV64)
#define NUCLEI_N_CPU TYPE_RISCV_CPU_NUCLEI_NX600FD
#endif

#endif
