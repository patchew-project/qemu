/*
 * TI AM64x SoC family model
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_TI_AM64X_H
#define HW_ARM_TI_AM64X_H

#include "system/memory.h"
#include "hw/arm/armv7m.h"
#include "cpu.h"
#include "hw/cpu/cluster.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/core/clock.h"
#include "qom/object.h"
#include "hw/misc/ti-rat.h"
#include "hw/misc/ti-sec-proxy.h"
#include "hw/misc/ti-dmsc.h"
#include "hw/misc/ti-mailbox.h"
#include "hw/misc/ti-k3-ctrlmmr.h"
#include "hw/misc/ti-k3-gtc.h"
#include "hw/misc/ti-k3-ddrss.h"
#include "hw/misc/ti-k3-sdhci-phy.h"
#include "hw/misc/ti-k3-trng.h"
#include "hw/sd/sdhci.h"
#include "hw/char/ti-am64-uart.h"
#include "hw/timer/ti-k3-dmtimer.h"

#define TYPE_TI_AM64X "ti-am64x"
OBJECT_DECLARE_SIMPLE_TYPE(TIAM64xState, TI_AM64X)

#define TI_AM64X_MCU_UART_NUM 2
#define TI_AM64X_MAILBOX_NUM 8
#define TI_AM64X_A53_NUM 2
#define TI_AM64X_GIC_NUM_SPI 256
#define TI_AM64X_R5_NUM 1
#define TI_AM64X_SDHCI_NUM 2

struct TIAM64xState {
    SysBusDevice parent_obj;
    CPUClusterState a53_cluster;
    CPUClusterState m4_cluster;
    CPUClusterState r5_cluster;
    ARMv7MState armv7m;
    ARMCPU a53[TI_AM64X_A53_NUM];
    ARMCPU r5[TI_AM64X_R5_NUM];
    GICv3State gic;
    MemoryRegion mcu_iram;
    MemoryRegion mcu_dram;
    MemoryRegion mcu_ddr;
    MemoryRegion mcu_iram_sysmem;
    MemoryRegion mcu_dram_sysmem;
    MemoryRegion mcu_root;
    MemoryRegion ocsram;
    Clock *sysclk;
    Clock *refclk;
    TIRATState rat;
    TISecProxyState sec_proxy;
    TIDmscState dmsc;
    TIMailboxState mailbox[TI_AM64X_MAILBOX_NUM];
    AM64Uart mcu_uart[TI_AM64X_MCU_UART_NUM];
    AM64Uart main_uart0;
    TIK3CtrlMmrState ctrlmmr;
    TIK3CtrlMmrState mcu_ctrlmmr;
    TIK3CtrlMmrState sec_ctrlmmr;
    TIK3DdrssState ddrss;
    TIK3TrngState trng;
    TIK3DmTimerState main_timer0;
    TIK3GtcState gtc;
    SDHCIState sdhci[TI_AM64X_SDHCI_NUM];
    TIK3SdhciPhyState sdhci_phy[TI_AM64X_SDHCI_NUM];
    /* MAIN domain I2C0, an OMAP-I2C V2 controller. */
    DeviceState *i2c0;
    uint64_t main_ram_base;
    uint64_t main_ram_size;
    uint8_t a53_cpus;
    bool a53_start_powered_off;
    bool m4_start_powered_off;
    bool r5_start_powered_off;
};

#endif
