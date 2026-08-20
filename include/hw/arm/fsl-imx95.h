/*
 * NXP i.MX 95 SoC definitions
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * Modeled on hw/arm/fsl-imx8mp.h
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Address and IRQ ground truth comes from two sources:
 *   - Linux DTS for everything Linux probes directly:
 *     arch/arm64/boot/dts/freescale/imx95.dtsi
 *   - NXP U-Boot's RM-derived header for the SCMI-routed peripherals
 *     U-Boot SPL pokes before SCMI is up:
 *     arch/arm/include/asm/arch-imx9/imx-regs.h
 * The latter set (CCM, ANATOP, IOMUXC, SRC, TRDC, the BLK_CTRL
 * aggregates) is currently logging-stub only - they are not modelled
 * because the NXP imx95-evk SPL routes all clock/pinmux/power-domain
 * access through SCMI to the M33 SM, which is the only SCMI provider.
 */

#ifndef FSL_IMX95_H
#define FSL_IMX95_H

#include "target/arm/cpu.h"
#include "hw/arm/armv7m.h"
#include "hw/char/imx_lpuart.h"
#include "hw/core/clock.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/misc/imx95_ele_server.h"
#include "hw/misc/imx_mu.h"
#include "hw/sd/sdhci.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/notify.h"
#include "qemu/units.h"

#define TYPE_FSL_IMX95 "fsl-imx95"
OBJECT_DECLARE_SIMPLE_TYPE(FslImx95State, FSL_IMX95)

/*
 * Main DDR window. i.MX 95 maps DRAM starting at 0x8000_0000 on both
 * the 19x19 (LPDDR5) and 15x15 (LPDDR4X) EVK variants.
 */
#define FSL_IMX95_RAM_START         0x80000000ULL
#define FSL_IMX95_RAM_SIZE_MAX      (16ULL * GiB)
/* Enough to hold the board's DTB (+128 MiB) and initrd (+256 MiB) presets. */
#define FSL_IMX95_RAM_SIZE_MIN      (512ULL * MiB)

/*
 * Cortex-M33 System Manager (SM) core. The SM firmware is linked to run
 * from the M33's tightly-coupled memories: code in ITCM, data/bss/stack
 * in DTCM. Sizes are rounded up to 256 KiB (the real TCM banks are
 * smaller; the SM image fits well within this). The reset vector table
 * sits at the ITCM base, so the M33's reset VTOR (init-svtor) points
 * there. The M33's 32-bit view is otherwise a superset of the A55 view
 * (it also reaches CCM/ANATOP/SRC/etc. that the A55 cannot) - we give it
 * the A55 system memory plus its private TCM; unmodelled SM-only
 * peripherals are backed by logging stubs.
 */
#define FSL_IMX95_M33_ITCM_BASE     0x1ffc0000ULL
#define FSL_IMX95_M33_DTCM_BASE     0x20000000ULL
#define FSL_IMX95_M33_TCM_SIZE      (256 * KiB)
#define FSL_IMX95_M33_SVTOR         0x1ffc0000U
#define FSL_IMX95_M33_NUM_IRQ       256
#define FSL_IMX95_M33_CLK_HZ        333333333U  /* SM runs the M33 ~333 MHz */

/*
 * Cortex-M7 real-time core. Memory map matches the upstream Linux
 * imx_rproc driver's imx_rproc_att_imx95_m7 table: the M7 sees ITCM at
 * its 0x00000000 (boot reset vector) and DTCM at 0x20000000; the A55
 * (and any other system-view observer) sees the same TCM RAM at
 * 0x203c0000 and 0x20400000 respectively, so a system-side firmware
 * loader (Linux remoteproc, U-Boot, ...) can populate the M7's TCM from
 * the A55 side. The M7's reset VTOR is 0 - the ARMv7-M default - because
 * ITCM is at M7-view 0 in this map.
 */
#define FSL_IMX95_M7_ITCM_M7VIEW    0x00000000ULL  /* M7's view of ITCM */
#define FSL_IMX95_M7_DTCM_M7VIEW    0x20000000ULL  /* M7's view of DTCM */
#define FSL_IMX95_M7_ITCM_SYSVIEW   0x203c0000ULL  /* A55's view of M7 ITCM */
#define FSL_IMX95_M7_DTCM_SYSVIEW   0x20400000ULL  /* A55's view of M7 DTCM */
#define FSL_IMX95_M7_TCM_SIZE       (256 * KiB)
#define FSL_IMX95_M7_NUM_IRQ        256
#define FSL_IMX95_M7_CLK_HZ         800000000U  /* M7 runs ~800 MHz */

/*
 * i.MX 95 application processor complex:
 *   - 6x Cortex-A55 (the main APUs)
 *   - 1x Cortex-M33 (System Manager, runs real NXP SM firmware)
 *   - 1x Cortex-M7  (real-time domain, runs an RT/MCUXpresso workload)
 * All three are instantiated; the M33 and M7 are heterogeneous CPU
 * contexts in the same QEMU instance, each with its own private TCMs
 * and ARMv7-M NVIC.
 */
enum FslImx95Configuration {
    FSL_IMX95_NUM_A55_CPUS  = 6,
    FSL_IMX95_NUM_LPUARTS   = 8,    /* LPUART1..LPUART8 */
    FSL_IMX95_NUM_USDHCS    = 3,    /* uSDHC1..uSDHC3 */
    FSL_IMX95_NUM_IRQS      = 320,  /* GIC SPI budget (max is 1020) */
};

struct FslImx95State {
    SysBusDevice            parent_obj;

    ARMCPU                  cpu[FSL_IMX95_NUM_A55_CPUS];

    /*
     * Cortex-M33 System Manager core. Wires the CPU + its private
     * TCM so the real NXP SM firmware (m33_image.elf) can be loaded and
     * started; the SM firmware it runs is this machine's only SCMI provider.
     * m33_view is the M33's 32-bit address space: ITCM + DTCM RAM layered
     * over a low-priority alias of the A55 system memory.
     *
     * The M33 starts powered-off and is released (on every system reset, by
     * fsl_imx95_reset) only if SM firmware was actually loaded into its ITCM;
     * without firmware it stays halted, so a plain A55 Linux boot is
     * unaffected. The same check releases the M7 by its own vector[0] test.
     */
    ARMv7MState             m33;
    MemoryRegion            m33_view;
    MemoryRegion            m33_sysmem_alias;
    MemoryRegion            m33_itcm;
    MemoryRegion            m33_dtcm;
    Clock                  *m33_cpuclk;

    /*
     * Cortex-M7 real-time core. Same structural pattern as the
     * M33: an ARMv7-M instance backed by its own ITCM + DTCM RAM regions
     * layered over a low-priority alias of the A55 system memory, in an
     * m7-private 32-bit view. The two *_sysalias regions expose the same
     * TCM RAM at the system-view addresses (0x203c0000 / 0x20400000) so
     * an A55-side loader can deposit M7 firmware into TCM from there;
     * the standalone "-device loader,file=cm7_image.elf,cpu-num=7" path
     * writes through the M7's own address space and does not need them.
     * The M7 starts powered-off and is released (on system reset, by
     * fsl_imx95_reset) alongside the M33, gated on a non-zero vector[0] in
     * its ITCM (i.e. firmware was actually loaded).
     */
    ARMv7MState             m7;
    MemoryRegion            m7_view;
    MemoryRegion            m7_sysmem_alias;
    MemoryRegion            m7_itcm;
    MemoryRegion            m7_dtcm;
    MemoryRegion            m7_itcm_sysalias;
    MemoryRegion            m7_dtcm_sysalias;
    Clock                  *m7_cpuclk;

    GICv3State              gic;
    MemoryRegion            ocram;
    MemoryRegion            sm_shmem;
    /* Alias of sm_shmem into the MU2 MUB window. */
    MemoryRegion            sm_shmem_b;
    /* Fuse Shadow Block (FSB) backing RAM - SM reads fuse words at boot. */
    MemoryRegion            fsb;
    /* VFCCU backing RAM - SM eMcem init writes fault config/flags. */
    MemoryRegion            vfccu;
    /* A55 CPU wait-semaphore SRAM (A1 MU SRAM page). */
    MemoryRegion            cpu_sema;
    /* CortexA TMPSNS backing RAM (SM sensor tick; CTRL0=0 -> filter idle). */
    MemoryRegion            tmpsns_ca;
    /* More eMcem/fabric init targets backed by RAM (write-acceptors). */
    MemoryRegion            vfccu_aon;
    MemoryRegion            erma;
    MemoryRegion            noc_sramctl;
    /*
     * System counter (timer@44290000): a real clockevent model with a live
     * counter + compare-match IRQ (hw/timer/imx95_sysctr.c). It is Linux's
     * tick BROADCAST device - the cpu-pd-wait idle state has local-timer-stop,
     * so an idling core shuts down its per-CPU arch timer and depends on this
     * counter's compare interrupt to wake. (The earlier RAM stub gave write-
     * then-read-back but no live counter / no IRQ, so idle cores never woke
     * and the boot needed cpuidle.off=1.)
     */
    DeviceState            *sysctr;
    /*
     * BBNSM (Battery-Backed Non-Secure Module: RTC, tamper, 8 general-
     * purpose registers) backed by RAM so writes stick. The NXP SM
     * firmware touches it during early init (brd_sm reset-record GPRs via
     * BBNSM_GprSetValue, and RTC setup that polls CTRL.RTC_EN back). The
     * driver only does write-then-read-back on these registers and never
     * reads VID/FEATURES, so a plain RAM region is sufficient. The A55
     * Linux side does not currently touch it.
     */
    MemoryRegion            bbnsm;
    /*
     * HSIO BLK_CTRL (HSIOMIX) backed by RAM. The SM's SystemInit does a
     * read-modify-write of the LFAST IO register at 0x4c0100c0; RAM gives
     * the read-back the write expects.
     */
    MemoryRegion            blk_ctrl_hsiomix;
    IMXLPUARTState          lpuart[FSL_IMX95_NUM_LPUARTS];
    IMXMUState              sm_mu;
    /* ELE MU the M33 SM uses for its EdgeLock channel (elemu0, 0x47520000). */
    IMXMUState              ele_mu0;
    IMXMUState              ele_mu1;
    /*
     * M33-side (MUB) endpoint of MU2, peer-linked to sm_mu (the A55-side
     * MUA @0x445b0000), mapped at 0x445c0000 with its IRQ routed to the M33
     * NVIC, so the real SM firmware services the A55's SCMI doorbells.
     */
    IMXMUState              sm_mu_b;
    /*
     * Stub MUs for the V2X / NETC / spare instances the DT references
     * but nothing in the model talks to. Wired with a NOP tr-write
     * handler so writes drop and TSR.TEn re-asserts, letting U-Boot
     * iterate them cleanly without hanging on TX-empty polls.
     */
    IMXMUState              stub_mu[6];
    /*
     * M7<->SM SCMI-over-SMT channel (MUI_A5), the mirror of the A55<->SM
     * MU2 cross-connect above for the M7's SCMI agent. m7_sm_mu is the
     * M7-side MUA @0x44610000; m7_sm_mu_b is the SM-side MUB @0x44620000,
     * peer-linked to it with its IRQ routed to the M33 NVIC. m7_shmem is
     * the 1 KiB SMT shared-memory page at MUA+0x1000 (0x44611000), with
     * m7_shmem_b aliasing the same RAM at MUB+0x1000 (0x44621000) so both
     * sides exchange messages through one buffer. The SM brings this MU up
     * eagerly in LMM_Init and waits interrupt-driven for the M7's doorbell.
     */
    IMXMUState              m7_sm_mu;
    IMXMUState              m7_sm_mu_b;
    MemoryRegion            m7_shmem;
    MemoryRegion            m7_shmem_b;
    /*
     * MU7: the A55<->M7 rpmsg notification mailbox (Linux cm7 remoteproc
     * kicks through it). mu7_a is the Linux-side MUA @0x42430000 (IRQ ->
     * GIC SPI 234); mu7_b is the M7-side MUB @0x42440000 (IRQ -> M7 NVIC
     * 207). Peer-linked so a TR write on one lands in the other's RR and
     * raises its RX interrupt; the vrings/buffers live in the 0x88000000
     * carveout (ordinary DRAM, seen by both cores).
     */
    IMXMUState              mu7_a;
    IMXMUState              mu7_b;
    IMX95ELEServerState     ele_server;
    /* ELE responder for the SM's elemu0 channel (0x47520000). */
    IMX95ELEServerState     ele_server0;
    /*
     * Second ELE server attached to the stub MU at 0x47550000. The
     * imx9_probe_mu() SCMI variant in U-Boot proper hardcodes
     * "mailbox@47550000" as the ELE channel (the SPL variant uses
     * "mailbox@47530000", which is ele_mu1). Without an ELE responder
     * at 0x47550000, ele_get_info() times out (-110), board_init_f's
     * initcall fails, and the boot hangs before serial_init prints.
     * Mirror the responder so both SPL and U-Boot proper reach it.
     */
    IMX95ELEServerState     ele_server2;
    SDHCIState              usdhc[FSL_IMX95_NUM_USDHCS];
    DeviceState            *wdog2;
    DeviceState            *wdog3;
    /* M33 XCACHE controllers (PC @0x44400000, PS @0x44400800). */
    DeviceState            *xcache_pc;
    DeviceState            *xcache_ps;
    /*
     * LPI2C the SM uses for the PMIC / IO-expander (SDK "LPI2C1" at
     * 0x44340000, our memmap FSL_IMX95_LPI2C7). Real master model with a
     * PF09 PMIC + PCAL6408A on its bus; the other LPI2C instances stay
     * logging stubs.
     */
    DeviceState            *lpi2c_pmic;
    /* GPC (General Power Controller) - SM power-domain / CPU-mode control. */
    DeviceState            *gpc;
    /* SRC (System Reset Controller) - SM mix-slice power-down/up control. */
    DeviceState            *src;
    /* BLK_CTRL_S_AONMIX - M7 CPU-WAIT gate (SM-managed M7 lifecycle). */
    DeviceState            *aonmix;
    /* ANATOP/PLL - SM DVFS PLL lock + DFS status (A55 perf level). */
    DeviceState            *anatop;

};

/*
 * Memory map region identifiers. The actual addresses live in the memmap
 * table in fsl-imx95.c.
 */
enum FslImx95MemoryRegions {
    FSL_IMX95_RAM,

    /* GIC-600 (GICv3-compatible) */
    FSL_IMX95_GIC_DIST,
    FSL_IMX95_GIC_REDIST,
    FSL_IMX95_GIC_ITS,

    /* On-chip SRAM "sram1" */
    FSL_IMX95_OCRAM,

    /* System counter (drives the ARM generic timer). */
    FSL_IMX95_SYSCNT,

    /* BBNSM (RTC / tamper / GPRs) - touched by the M33 SM firmware. */
    FSL_IMX95_BBNSM,

    /* M33 XCACHE controllers (enabled/invalidated by the SM at boot). */
    FSL_IMX95_XCACHE_PC,
    FSL_IMX95_XCACHE_PS,

    /* HSIO BLK_CTRL (HSIOMIX) - SM SystemInit RMWs the LFAST IO reg. */
    FSL_IMX95_BLK_CTRL_HSIOMIX,

    /* GPC (per-domain CPU_CTRL + GLOBAL) - SM power/CPU-mode control. */
    FSL_IMX95_GPC,

    /* LPUART block (8 instances; all modelled — TYPE_IMX_LPUART) */
    FSL_IMX95_LPUART1,
    FSL_IMX95_LPUART2,
    FSL_IMX95_LPUART3,
    FSL_IMX95_LPUART4,
    FSL_IMX95_LPUART5,
    FSL_IMX95_LPUART6,
    FSL_IMX95_LPUART7,
    FSL_IMX95_LPUART8,

    /*
     * Clock / reset / pinmux infrastructure. SCMI-routed when M33 SM
     * is up; SPL touches them only before SCMI, kernel never. Logging
     * stubs.
     */
    FSL_IMX95_CCM,
    FSL_IMX95_ANATOP,
    FSL_IMX95_IOMUXC,
    FSL_IMX95_SRC,
    FSL_IMX95_TRDC_AON,

    /* BLK_CTRL aggregates per power domain. Logging stubs. */
    FSL_IMX95_BLK_CTRL_S_AONMIX,
    FSL_IMX95_BLK_CTRL_NS_ANOMIX,
    FSL_IMX95_BLK_CTRL_WAKEUPMIX,
    FSL_IMX95_BLK_CTRL_NETCMIX,

    /* System Manager SCMI: mu2 mailbox + sram0 shared-memory buffer */
    FSL_IMX95_SM_MU,
    FSL_IMX95_SM_SHMEM,

    /* EdgeLock Secure Enclave mailboxes (elemu0/elemu1 needed for SPL) */
    FSL_IMX95_ELE_MU,
    FSL_IMX95_ELE_MU1,

    /* Fuse Shadow Block - read by the SM's DEV_SM_FuseInit at boot. */
    FSL_IMX95_FSB,

    /* VFCCU (Fault Collection & Control Unit) - SM eMcem init touches it. */
    FSL_IMX95_VFCCU,
    /* A1 MU SRAM page holding the A55 CPU wait-semaphore (DEV_SM_CpuInit). */
    FSL_IMX95_CPU_SEMA,
    /* CortexA TMPSNS - SM sensor tick reads it (post-init periodic task). */
    FSL_IMX95_TMPSNS_CA,
    /* More SM eMcem/fabric init targets: AON_VFCCU, AON_ERMA, NOC_SRAMCTL. */
    FSL_IMX95_VFCCU_AON,
    FSL_IMX95_ERMA,
    FSL_IMX95_NOC_SRAMCTL,

    /*
     * Other MU instances U-Boot proper probes from DT (mailbox@*).
     * Not yet modelled; logging stubs are enough for mu_hal_init to
     * complete cleanly (it reads PAR, writes RCR/TCR=0, reads SR — all
     * zeros are accepted and the drain loop is skipped).
     */
    FSL_IMX95_MU_47320000,
    FSL_IMX95_MU_47350000,
    FSL_IMX95_MU_47540000,
    FSL_IMX95_MU_47550000,
    FSL_IMX95_MU_47560000,
    FSL_IMX95_MU_47570000,

    /*
     * Watchdogs. SPL disables WDG3/4/5 in arch_cpu_init(). WDOG2 is the
     * M33 SM's own watchdog (unlocked + configured in its reset handler).
     */
    FSL_IMX95_WDOG2,
    FSL_IMX95_WDOG3,
    FSL_IMX95_WDOG4,
    FSL_IMX95_WDOG5,

    /* GPIO controllers (1..5). SPL gpio_reset()s 2..5; #1 added for parity. */
    FSL_IMX95_GPIO1,
    FSL_IMX95_GPIO2,
    FSL_IMX95_GPIO3,
    FSL_IMX95_GPIO4,
    FSL_IMX95_GPIO5,

    /* ARM SMMU-v3 - SPL's disable_smmuv3() reads CR0; stub returns 0. */
    FSL_IMX95_SMMU,

    /*
     * uSDHC (SD / eMMC) controllers. SPL probes them to find a boot
     * device; stubbed so we can observe access patterns before
     * promoting to a real fsl-esdhc model.
     */
    FSL_IMX95_USDHC1,
    FSL_IMX95_USDHC2,
    FSL_IMX95_USDHC3,

    /*
     * LPI2C controllers. U-Boot proper probes them during the post-
     * banner dram_init / board_init phase (PMIC over I2C2 is the
     * critical one - PCA9450 on the EVK). Logging stubs are enough
     * to let the I2C transfer time out gracefully and DRAM init
     * fall back to defaults.
     */
    FSL_IMX95_LPI2C1,
    FSL_IMX95_LPI2C2,
    FSL_IMX95_LPI2C3,
    FSL_IMX95_LPI2C4,
    FSL_IMX95_LPI2C5,
    FSL_IMX95_LPI2C6,
    FSL_IMX95_LPI2C7,
    FSL_IMX95_LPI2C8,

    /*
     * USB PHY + DWC3 controller. U-Boot autoboot's bootcmd touches
     * these via dwc3_imx8mp_glue_configure (reads at 0x4c1f0000).
     * Logging stubs prevent the data abort that otherwise resets
     * the board mid-autoboot.
     */
    FSL_IMX95_USB_PHY,
    FSL_IMX95_USB_DWC3,

    FSL_IMX95_NUM_REGIONS,
};

/*
 * IRQ assignments. All values are GIC SPI numbers extracted from
 * Linux arch/arm64/boot/dts/freescale/imx95.dtsi.
 */
enum FslImx95Irqs {
    FSL_IMX95_LPUART1_IRQ   = 19,
    FSL_IMX95_LPUART2_IRQ   = 20,
    FSL_IMX95_LPUART3_IRQ   = 64,
    FSL_IMX95_LPUART4_IRQ   = 65,
    FSL_IMX95_LPUART5_IRQ   = 66,
    FSL_IMX95_LPUART6_IRQ   = 67,
    FSL_IMX95_LPUART7_IRQ   = 68,
    FSL_IMX95_LPUART8_IRQ   = 69,
    FSL_IMX95_WDOG3_IRQ     = 77,
    /* uSDHC1/2/3 SPIs (dtsi: usdhc1=86, usdhc2=87, usdhc3=191). */
    FSL_IMX95_USDHC1_IRQ    = 86,
    FSL_IMX95_USDHC2_IRQ    = 87,
    FSL_IMX95_USDHC3_IRQ    = 191,
    /* system counter compare-match (dtsi: timer@44290000). */
    FSL_IMX95_SYSCNT_IRQ    = 72,
    /* mu2 is the SCMI mailbox to the M33 SM (dtsi:1655). */
    FSL_IMX95_SM_MU_IRQ     = 226,
};

/*
 * M33 NVIC external interrupt number for the MU2 B-side (MU2_B_IRQn in the
 * SM's MIMX95 device header) - the IRQ the SM enables to receive A55 SCMI
 * doorbells on its side of MU2.
 */
#define FSL_IMX95_SM_MU_B_M33_IRQ   227

/*
 * MUI_A5 (the M7<->SM SCMI channel) interrupt numbers, from the SM's
 * MIMX95 device header (MIMX95_COMMON.h IRQn enum):
 *   - MU5_A_IRQn = 205: the M7-side IRQ, raised on the M7's NVIC when the
 *     SM rings its doorbell back (m7_sm_mu @0x44610000 -> M7).
 *   - MU5_B_IRQn = 232: the SM-side IRQ, raised on the M33's NVIC when the
 *     M7 rings its doorbell (m7_sm_mu_b @0x44620000 -> M33). The SM enables
 *     this in MB_MU_Init for MU9 and dispatches SCMI from its handler.
 */
#define FSL_IMX95_M7_SM_MU_M7_IRQ   205
#define FSL_IMX95_M7_SM_MU_B_M33_IRQ 232

/*
 * MU7 (A55<->M7 rpmsg) interrupts. MUA is the Linux side: GIC SPI 234
 * (= MU7_A_IRQn, matching the dtsi mu7 node). MUB is the M7 side: M7 NVIC
 * 207 (= MU7_B_IRQn in MIMX95_COMMON.h).
 */
#define FSL_IMX95_MU7_A_IRQ         234
#define FSL_IMX95_MU7_B_M7_IRQ      207

/*
 * M33 NVIC inputs for the Cortex-M7 fault/reset sources the SRC routes to
 * the System Manager (MIMX95_COMMON.h IRQn enum). When the M7 asserts
 * SYSRESETREQ (or locks up) the SM takes the fault and, for an LM with
 * reaction=lm_reset (the M7 LM on mx95evk), cold-resets the M7 LM via the
 * SRC M7 mix-slice. We wire the M7's NVIC SYSRESETREQ gpio-out to
 * CM7_SYSRESETREQ_IRQn so the real SM firmware drives the recovery.
 */
#define FSL_IMX95_M7_SYSRESETREQ_M33_IRQ 167
#define FSL_IMX95_M7_LOCKUP_M33_IRQ      168

#endif /* FSL_IMX95_H */
