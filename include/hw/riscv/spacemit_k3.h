/*
 * Spacemit K3 Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#ifndef HW_RISCV_SPACEMIT_K3_H
#define HW_RISCV_SPACEMIT_K3_H

#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sd/sdhci.h"
#include "hw/misc/spacemit_k3_syscon.h"
#include "hw/ssi/spacemit_k3_spi.h"
#include "hw/ufs/spacemit_k3_ufs.h"

#define TYPE_SPACEMIT_K3_MACHINE MACHINE_TYPE_NAME("spacemit-k3")
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3State, SPACEMIT_K3_MACHINE);

#define SPACEMIT_K3_NUM_SDHCI 2

struct SpacemitK3State {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    const MemMapEntry *memmap;

    RISCVHartArrayState soc;
    DeviceState *irqchip;

    SDHCIState sdhci[SPACEMIT_K3_NUM_SDHCI], emmc;
    SpacemitK3DDRCState ddrc0, ddrc1;
    SpacemitK3CPUConfigState cpu_config;
    SpacemitK3SysconAPMUState syscon_apmu;
    SpacemitK3SysconMPMUState syscon_mpmu;
    SpacemitK3SpiState qspi;
    SpacemitK3UFSState ufs;

    int boot_mode;
};

enum {
    SPACEMIT_K3_CPU_CONFIG_UNIT,
    SPACEMIT_K3_SEC_JTAG_SOFTWARE,
    SPACEMIT_K3_SEC_APB_CLK_UNIT,
    SPACEMIT_K3_SEC_BCM_CONFIG,
    SPACEMIT_K3_PLL,
    SPACEMIT_K3_SYSCON_MPMU,
    SPACEMIT_K3_SYSCON_APMU,
    SPACEMIT_K3_SYSCON_APBC,
    SPACEMIT_K3_UART0,
    SPACEMIT_K3_PINCTRL,
    SPACEMIT_K3_COUNTER,
    SPACEMIT_K3_SDHCI0,
    SPACEMIT_K3_SDHCI1,
    SPACEMIT_K3_EMMC,
    SPACEMIT_K3_BOOTROM,
    SPACEMIT_K3_SRAM,
    SPACEMIT_K3_DDR_TRAIN_BUF,
    SPACEMIT_K3_AUDIO_PERIPH,
    SPACEMIT_K3_DDRC0,
    SPACEMIT_K3_DDRC1,
    SPACEMIT_K3_DRAM,
    SPACEMIT_K3_MIMSIC,
    SPACEMIT_K3_SIMSIC,
    SPACEMIT_K3_MAPLIC,
    SPACEMIT_K3_SAPLIC,
    SPACEMIT_K3_ACLINT,
    SPACEMIT_K3_I2C1,
    SPACEMIT_K3_I2C2,
    SPACEMIT_K3_I2C6,
    SPACEMIT_K3_I2C8,
    SPACEMIT_K3_ESPI,
    SPACEMIT_K3_MAILBOX3,
    SPACEMIT_K3_SYSCON_DCIU,
    SPACEMIT_K3_CCI550,
    SPACEMIT_K3_ESPI_SLV_MEM,
    SPACEMIT_K3_WDT0,
    SPACEMIT_K3_DP1,
    SPACEMIT_K3_GPIO,
    SPACEMIT_K3_QSPI,
    SPACEMIT_K3_QSPI_AHB,
    SPACEMIT_K3_UFSHC,
    SPACEMIT_K3_PCIE,
    SPACEMIT_K3_USB_OTG,
    SPACEMIT_K3_ETH0,
    SPACEMIT_K3_USB3,
    SPACEMIT_K3_THERMAL,
};

enum {
    SPACEMIT_K3_UART0_IRQ = 42,
    SPACEMIT_K3_SDHCI0_IRQ = 99,
    SPACEMIT_K3_SDHCI1_IRQ = 100,
    SPACEMIT_K3_EMMC_IRQ = 101,
    SPACEMIT_K3_QSPI_IRQ  = 117,
    SPACEMIT_K3_UFSHC_IRQ  = 135,
};

#endif
