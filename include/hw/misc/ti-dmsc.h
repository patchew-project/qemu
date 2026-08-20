/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * TI DMSC emulator device (minimal TISCI service)
 *
 * This device links to a TI SEC_PROXY instance and provides the minimal
 * DMSC/TISCI request/response handler.
 */

#ifndef HW_MISC_TI_DMSC_H
#define HW_MISC_TI_DMSC_H

#include "qemu/compiler.h"
#include "hw/core/qdev.h"
#include "hw/misc/ti-sec-proxy.h"

#define TISCI_MSG_VALUE_DEVICE_SW_STATE_AUTO_OFF 0

/** Used by TISCI_MSG_SET_DEVICE to disable device but keep in retention. */
#define TISCI_MSG_VALUE_DEVICE_SW_STATE_RETENTION 1

/** Used by TISCI_MSG_SET_DEVICE to turn device on for usage. */
#define TISCI_MSG_VALUE_DEVICE_SW_STATE_ON 2

/* Device is off in TISCI_MSG_GET_DEVICE response. */
#define TISCI_MSG_VALUE_DEVICE_HW_STATE_OFF 0

/* Device is on in TISCI_MSG_GET_DEVICE response. */
#define TISCI_MSG_VALUE_DEVICE_HW_STATE_ON 1

/*
 * Device is changing state. The state may remain until dependent hardware,
 * e.g. pending IRQ handshakes, allows to complete the transition.
 */
#define TISCI_MSG_VALUE_DEVICE_HW_STATE_TRANS 2

/** DMSC(Secure): Device Management and Security Control */
#define TISCI_HOST_ID_DMSC (0U)
/** MAIN_0_R5_0(Secure): Cortex R5_0 context 0 on Main island(BOOT) */
#define TISCI_HOST_ID_MAIN_0_R5_0 (35U)
/** MAIN_0_R5_1(Non Secure): Cortex R5_0 context 1 on Main island */
#define TISCI_HOST_ID_MAIN_0_R5_1 (36U)
/** MAIN_0_R5_2(Secure): Cortex R5_0 context 2 on Main island */
#define TISCI_HOST_ID_MAIN_0_R5_2 (37U)
/** MAIN_0_R5_3(Non Secure): Cortex R5_0 context 3 on Main island */
#define TISCI_HOST_ID_MAIN_0_R5_3 (38U)
/** A53_0(Secure): Cortex a53 context 0 on Main island */
#define TISCI_HOST_ID_A53_0 (10U)
/** A53_1(Secure): Cortex A53 context 1 on Main island */
#define TISCI_HOST_ID_A53_1 (11U)
/** A53_2(Non Secure): Cortex A53 context 2 on Main island */
#define TISCI_HOST_ID_A53_2 (12U)
/** A53_3(Non Secure): Cortex A53 context 3 on Main island */
#define TISCI_HOST_ID_A53_3 (13U)
/** M4_0(Non Secure): M4 */
#define TISCI_HOST_ID_M4_0 (30U)
/** MAIN_1_R5_0(Secure): Cortex R5_1 context 0 on Main island */
#define TISCI_HOST_ID_MAIN_1_R5_0 (40U)
/** MAIN_1_R5_1(Non Secure): Cortex R5_1 context 1 on Main island */
#define TISCI_HOST_ID_MAIN_1_R5_1 (41U)
/** MAIN_1_R5_2(Secure): Cortex R5_1 context 2 on Main island */
#define TISCI_HOST_ID_MAIN_1_R5_2 (42U)
/** MAIN_1_R5_3(Non Secure): Cortex R5_1 context 3 on Main island */
#define TISCI_HOST_ID_MAIN_1_R5_3 (43U)
/** A53_4(Non Secure): Cortex A53 context 1 on Main island */
#define TISCI_HOST_ID_A53_4 (14U)
/** ICSSG_0(Non Secure): ICSSG context 0 on Main island */
#define TISCI_HOST_ID_ICSSG_0 (50U)
/** ICSSG_1(Non Secure): ICSSG context 1 on Main island */
#define TISCI_HOST_ID_ICSSG_1 (51U)

/* Catch-all host for board-config resource assignments. */
#define TISCI_HOST_ID_ALL (128U)

/** Number of unique hosts on the SoC */
#define TISCI_HOST_ID_CNT (17U)

#define TISCI_DEV_ADC0 0U
#define TISCI_DEV_CMP_EVENT_INTROUTER0 1U
#define TISCI_DEV_DBGSUSPENDROUTER0 2U
#define TISCI_DEV_MAIN_GPIOMUX_INTROUTER0 3U
#define TISCI_DEV_MCU_MCU_GPIOMUX_INTROUTER0 5U
#define TISCI_DEV_TIMESYNC_EVENT_INTROUTER0 6U
#define TISCI_DEV_MCU_M4FSS0 7U
#define TISCI_DEV_MCU_M4FSS0_CBASS_0 8U
#define TISCI_DEV_MCU_M4FSS0_CORE0 9U
#define TISCI_DEV_CPSW0 13U
#define TISCI_DEV_CPT2_AGGR0 14U
#define TISCI_DEV_STM0 15U
#define TISCI_DEV_DCC0 16U
#define TISCI_DEV_DCC1 17U
#define TISCI_DEV_DCC2 18U
#define TISCI_DEV_DCC3 19U
#define TISCI_DEV_DCC4 20U
#define TISCI_DEV_DCC5 21U
#define TISCI_DEV_DMSC0 22U
#define TISCI_DEV_MCU_DCC0 23U
#define TISCI_DEV_DEBUGSS_WRAP0 24U
#define TISCI_DEV_DMASS0 25U
#define TISCI_DEV_DMASS0_BCDMA_0 26U
#define TISCI_DEV_DMASS0_CBASS_0 27U
#define TISCI_DEV_DMASS0_INTAGGR_0 28U
#define TISCI_DEV_DMASS0_IPCSS_0 29U
#define TISCI_DEV_DMASS0_PKTDMA_0 30U
#define TISCI_DEV_DMASS0_RINGACC_0 33U
#define TISCI_DEV_MCU_TIMER0 35U
#define TISCI_DEV_TIMER0 36U
#define TISCI_DEV_TIMER1 37U
#define TISCI_DEV_TIMER2 38U
#define TISCI_DEV_TIMER3 39U
#define TISCI_DEV_TIMER4 40U
#define TISCI_DEV_TIMER5 41U
#define TISCI_DEV_TIMER6 42U
#define TISCI_DEV_TIMER7 43U
#define TISCI_DEV_TIMER8 44U
#define TISCI_DEV_TIMER9 45U
#define TISCI_DEV_TIMER10 46U
#define TISCI_DEV_TIMER11 47U
#define TISCI_DEV_MCU_TIMER1 48U
#define TISCI_DEV_MCU_TIMER2 49U
#define TISCI_DEV_MCU_TIMER3 50U
#define TISCI_DEV_ECAP0 51U
#define TISCI_DEV_ECAP1 52U
#define TISCI_DEV_ECAP2 53U
#define TISCI_DEV_ELM0 54U
#define TISCI_DEV_EMIF_DATA_0_VD 55U
#define TISCI_DEV_MMCSD0 57U
#define TISCI_DEV_MMCSD1 58U
#define TISCI_DEV_EQEP0 59U
#define TISCI_DEV_EQEP1 60U
#define TISCI_DEV_GTC0 61U
#define TISCI_DEV_EQEP2 62U
#define TISCI_DEV_ESM0 63U
#define TISCI_DEV_MCU_ESM0 64U
#define TISCI_DEV_FSIRX0 65U
#define TISCI_DEV_FSIRX1 66U
#define TISCI_DEV_FSIRX2 67U
#define TISCI_DEV_FSIRX3 68U
#define TISCI_DEV_FSIRX4 69U
#define TISCI_DEV_FSIRX5 70U
#define TISCI_DEV_FSITX0 71U
#define TISCI_DEV_FSITX1 72U
#define TISCI_DEV_FSS0 73U
#define TISCI_DEV_FSS0_FSAS_0 74U
#define TISCI_DEV_FSS0_OSPI_0 75U
#define TISCI_DEV_GICSS0 76U
#define TISCI_DEV_GPIO0 77U
#define TISCI_DEV_GPIO1 78U
#define TISCI_DEV_MCU_GPIO0 79U
#define TISCI_DEV_GPMC0 80U
#define TISCI_DEV_PRU_ICSSG0 81U
#define TISCI_DEV_PRU_ICSSG1 82U
#define TISCI_DEV_LED0 83U
#define TISCI_DEV_CPTS0 84U
#define TISCI_DEV_DDPA0 85U
#define TISCI_DEV_EPWM0 86U
#define TISCI_DEV_EPWM1 87U
#define TISCI_DEV_EPWM2 88U
#define TISCI_DEV_EPWM3 89U
#define TISCI_DEV_EPWM4 90U
#define TISCI_DEV_EPWM5 91U
#define TISCI_DEV_EPWM6 92U
#define TISCI_DEV_EPWM7 93U
#define TISCI_DEV_EPWM8 94U
#define TISCI_DEV_VTM0 95U
#define TISCI_DEV_MAILBOX0 96U
#define TISCI_DEV_MAIN2MCU_VD 97U
#define TISCI_DEV_MCAN0 98U
#define TISCI_DEV_MCAN1 99U
#define TISCI_DEV_MCU_MCRC64_0 100U
#define TISCI_DEV_MCU2MAIN_VD 101U
#define TISCI_DEV_I2C0 102U
#define TISCI_DEV_I2C1 103U
#define TISCI_DEV_I2C2 104U
#define TISCI_DEV_I2C3 105U
#define TISCI_DEV_MCU_I2C0 106U
#define TISCI_DEV_MCU_I2C1 107U
#define TISCI_DEV_PCIE0 114U
#define TISCI_DEV_R5FSS0 119U
#define TISCI_DEV_R5FSS1 120U
#define TISCI_DEV_R5FSS0_CORE0 121U
#define TISCI_DEV_R5FSS0_CORE1 122U
#define TISCI_DEV_R5FSS1_CORE0 123U
#define TISCI_DEV_R5FSS1_CORE1 124U
#define TISCI_DEV_RTI0 125U
#define TISCI_DEV_RTI1 126U
#define TISCI_DEV_RTI8 127U
#define TISCI_DEV_RTI9 128U
#define TISCI_DEV_RTI10 130U
#define TISCI_DEV_RTI11 131U
#define TISCI_DEV_MCU_RTI0 132U
#define TISCI_DEV_SA2_UL0 133U
#define TISCI_DEV_COMPUTE_CLUSTER0 134U
#define TISCI_DEV_A53SS0_CORE_0 135U
#define TISCI_DEV_A53SS0_CORE_1 136U
#define TISCI_DEV_A53SS0 137U
#define TISCI_DEV_DDR16SS0 138U
#define TISCI_DEV_PSC0 139U
#define TISCI_DEV_MCU_PSC0 140U
#define TISCI_DEV_MCSPI0 141U
#define TISCI_DEV_MCSPI1 142U
#define TISCI_DEV_MCSPI2 143U
#define TISCI_DEV_MCSPI3 144U
#define TISCI_DEV_MCSPI4 145U
#define TISCI_DEV_UART0 146U
#define TISCI_DEV_MCU_MCSPI0 147U
#define TISCI_DEV_MCU_MCSPI1 148U
#define TISCI_DEV_MCU_UART0 149U
#define TISCI_DEV_SPINLOCK0 150U
#define TISCI_DEV_TIMERMGR0 151U
#define TISCI_DEV_UART1 152U
#define TISCI_DEV_UART2 153U
#define TISCI_DEV_UART3 154U
#define TISCI_DEV_UART4 155U
#define TISCI_DEV_UART5 156U
#define TISCI_DEV_BOARD0 157U
#define TISCI_DEV_UART6 158U
#define TISCI_DEV_MCU_UART1 160U
#define TISCI_DEV_USB0 161U
#define TISCI_DEV_SERDES_10G0 162U
#define TISCI_DEV_PBIST0 163U
#define TISCI_DEV_PBIST1 164U
#define TISCI_DEV_PBIST2 165U
#define TISCI_DEV_PBIST3 166U
#define TISCI_DEV_COMPUTE_CLUSTER0_PBIST_0 167U
#define TISCI_DEV_ID_MAX 168U

#define TISCI_MSG_FLAG_RESERVED0 BIT(0)
/*
 * ACK-on-processed: request a response after handling, ACK on success and
 * NAK otherwise.
 */
#define TISCI_MSG_FLAG_AOP BIT(1)

/** Indicate that this message is marked secure */
#define TISCI_MSG_FLAG_SEC BIT(2)

/* Response success flag; missing one means NAK. */
#define TISCI_MSG_FLAG_ACK BIT(1)

/* TISCI Message IDs */
#define TISCI_MSG_VERSION (0x0002U)
#define TISCI_MSG_BOOT_NOTIFICATION (0x000AU)
#define TISCI_MSG_BOARD_CONFIG (0x000BU)
#define TISCI_MSG_BOARD_CONFIG_RM (0x000CU)
#define TISCI_MSG_BOARD_CONFIG_SECURITY (0x000DU)
#define TISCI_MSG_BOARD_CONFIG_PM (0x000EU)

#define TISCI_MSG_ENABLE_WDT (0x0000U)
#define TISCI_MSG_WAKE_RESET (0x0001U)
#define TISCI_MSG_WAKE_REASON (0x0003U)
#define TISCI_MSG_GOODBYE (0x0004U)
#define TISCI_MSG_SYS_RESET (0x0005U)

#define TISCI_MSG_QUERY_MSMC (0x0020U)
#define TISCI_MSG_GET_TRACE_CONFIG (0x0021U)
#define TISCI_MSG_QUERY_FW_CAPS (0x0022U)

#define TISCI_MSG_SET_CLOCK (0x0100U)
#define TISCI_MSG_GET_CLOCK (0x0101U)
#define TISCI_MSG_SET_CLOCK_PARENT (0x0102U)
#define TISCI_MSG_GET_CLOCK_PARENT (0x0103U)
#define TISCI_MSG_GET_NUM_CLOCK_PARENTS (0x0104U)
#define TISCI_MSG_SET_FREQ (0x010cU)
#define TISCI_MSG_QUERY_FREQ (0x010dU)
#define TISCI_MSG_GET_FREQ (0x010eU)

#define TISCI_MSG_SET_DEVICE (0x0200U)
#define TISCI_MSG_GET_DEVICE (0x0201U)

#define TISCI_MSG_SET_DEVICE_RESETS (0x0202U)
#define TISCI_MSG_DEVICE_DROP_POWERUP_REF (0x0203U)

#define TISCI_MSG_PREPARE_SLEEP (0x0300U)
#define TISCI_MSG_ENTER_SLEEP (0x0301U)

#define TISCI_MSG_PROC_REQUEST (0xc000U)
#define TISCI_MSG_PROC_RELEASE (0xc001U)
#define TISCI_MSG_PROC_HANDOVER (0xc005U)
#define TISCI_MSG_SET_CONFIG (0xc100U)
#define TISCI_MSG_SET_CTRL (0xc101U)
#define TISCI_MSG_GET_STATUS (0xc400U)
#define TISCI_MSG_WAIT_PROC_BOOT_STATUS (0xc401U)

/*
 * Security message IDs for K3 SA2UL, OTP and secure-boot TI-SCI services.
 * Layouts match the U-Boot/Zephyr TI-SCI protocol headers.
 */
#define TISCI_MSG_FWL_SET (0x9000U)
#define TISCI_MSG_FWL_GET (0x9001U)
#define TISCI_MSG_FWL_CHANGE_OWNER (0x9002U)
#define TISCI_MSG_SA2UL_GET_DKEK (0x9029U)
#define TISCI_MSG_READ_SWREV (0x9033U)
#define TISCI_MSG_READ_KEYCNT_KEYREV (0x9034U)

#define TISCI_MSG_MAX_ID (0xc500U)

/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 9 Processor 0) */
#define SCICLIENT_PROCID_A53_CL0_C0 (0x20U)
/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 9 Processor 1) */
#define SCICLIENT_PROCID_A53_CL0_C1 (0x21U)
/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 0 Processor 0) */
#define SCICLIENT_PROCID_R5_CL0_C0 (0x01U)
/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 0 Processor 1) */
#define SCICLIENT_PROCID_R5_CL0_C1 (0x02U)
/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 1 Processor 0) */
#define SCICLIENT_PROCID_R5_CL1_C0 (0x06U)
/** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 1 Processor 1) */
#define SCICLIENT_PROCID_R5_CL1_C1 (0x07U)
/*** AM64_MAIN_SEC_MMR_MAIN_0: (Cluster 16 Processor 0) */
#define SCICLIENT_PROCID_MCU_M4FSS0_C0 (0x18U)

#define TYPE_TI_DMSC "ti-dmsc"

OBJECT_DECLARE_SIMPLE_TYPE(TIDmscState, TI_DMSC)

/* Default: 64 bytes -> 16 words */
#define TI_DMSC_MAX_WORDS 16

/*
 * Minimal TISCI wire structs. Keep them packed and model only fields this
 * device actually consumes or returns.
 */
typedef struct TISciMsgHdr {
    uint16_t type;
    uint8_t host;
    uint8_t seq;
    uint32_t flags;
} QEMU_PACKED TISciMsgHdr;

struct TiSciMsgReqProcRequest {
    TISciMsgHdr hdr;
    uint8_t processor_id;
} QEMU_PACKED;

struct TiSciMsgReqProcRelease {
    TISciMsgHdr hdr;
    uint8_t processor_id;
} QEMU_PACKED;

/*
 * TISCI_MSG_PROC_HANDOVER request. Response is only TISciMsgHdr ACK/NAK.
 */
struct TiSciMsgReqProcHandover {
    TISciMsgHdr hdr;
    uint8_t processor_id;
    uint8_t host_id;
} QEMU_PACKED;

#define TISCI_MSG_VAL_PROC_BOOT_STATUS_FLAG_M4F_WFI (0x00000002U)

struct TisciMsgProcGetStatusReq {
    TISciMsgHdr hdr;
    uint8_t processor_id;
} QEMU_PACKED;

struct TisciMsgProcGetStatusResp {
    TISciMsgHdr hdr;
    uint8_t processor_id;
    uint32_t bootvector_lo;
    uint32_t bootvector_hi;
    uint32_t config_flags_1;
    uint32_t control_flags_1;
    uint32_t status_flags_1;
} QEMU_PACKED;

/*
 * WAIT_PROC_BOOT_STATUS request. Only processor_id is consumed by the no-op
 * handler, so the trailing wait/status fields are left out.
 */
struct TisciMsgReqWaitProcBootStatus {
    TISciMsgHdr hdr;
    uint8_t processor_id;
} QEMU_PACKED;

/*
 * SET_DEVICE matches the TISCI ABI layout: the reserved u32 before state is
 * on the wire and keeps the state byte aligned to SYSFW.
 */
struct TisciMsgSetDeviceReq {
    TISciMsgHdr hdr;
    uint32_t id;
    uint32_t reserved;
    uint8_t state;
} QEMU_PACKED;

struct TisciMsgSetDeviceResetsReq {
    TISciMsgHdr hdr;
    uint32_t id;
    uint32_t resets;
} QEMU_PACKED;

struct TiSciMsgQueryFwCapsResp {
    TISciMsgHdr hdr;
#define MSG_FLAG_CAPS_GENERIC BIT(0)
#define MSG_FLAG_CAPS_LPM_DEEP_SLEEP BIT(1)
#define MSG_FLAG_CAPS_LPM_MCU_ONLY BIT(2)
#define MSG_FLAG_CAPS_LPM_STANDBY BIT(3)
#define MSG_FLAG_CAPS_LPM_PARTIAL_IO BIT(4)
#define MSG_FLAG_CAPS_LPM_DM_MANAGED BIT(5)
    uint64_t fw_caps;
} QEMU_PACKED;

struct TiSciMsgVersionResp {
    TISciMsgHdr hdr;
    char firmware_description[32];
    uint16_t firmware_revision;
    uint8_t abi_major;
    uint8_t abi_minor;
} QEMU_PACKED;

struct TisciMsgSetFreqReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint64_t min_freq_hz;
    uint64_t target_freq_hz;
    uint64_t max_freq_hz;
    uint8_t clk;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgQueryFreqReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint64_t min_freq_hz;
    uint64_t target_freq_hz;
    uint64_t max_freq_hz;
    uint8_t clk;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgQueryFreqResp {
    TISciMsgHdr hdr;
    uint64_t freq_hz;
} QEMU_PACKED;

/*
 * GET_FREQ has only device/clock in the request. The response is hdr plus
 * freq_hz, same payload as QUERY_FREQ.
 */
struct TisciMsgGetFreqReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint8_t clk;
} QEMU_PACKED;

struct TisciMsgSetClockReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint8_t clk;
    uint8_t state;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgGetNumClockParentsReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint8_t clk;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgGetNumClockParentsResp {
    TISciMsgHdr hdr;
    uint8_t num_parents;
    uint32_t num_parentint32_t;
} QEMU_PACKED;

struct TisciMsgGetClockParentReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint8_t clk;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgGetClockParentResp {
    TISciMsgHdr hdr;
    uint8_t parent;
    uint32_t parent32;
} QEMU_PACKED;

/*
 * TISCI_MSG_SET_CLOCK_PARENT request. Response is bare TISciMsgHdr ACK/NAK.
 */
struct TisciMsgSetClockParentReq {
    TISciMsgHdr hdr;
    uint32_t dev_id;
    uint8_t clk_id;
    uint8_t parent_id;
} QEMU_PACKED;

struct TisciMsgGetClockReq {
    TISciMsgHdr hdr;
    uint32_t device;
    uint8_t clk;
    uint32_t clk32;
} QEMU_PACKED;

struct TisciMsgGetClockResp {
    TISciMsgHdr hdr;
    uint8_t programmed_state;
    uint8_t current_state;
} QEMU_PACKED;

struct TisciMsgGetDeviceReq {
    TISciMsgHdr hdr;
    uint32_t id;
} QEMU_PACKED;

struct TisciMsgGetDeviceResp {
    TISciMsgHdr hdr;
    uint32_t context_loss_count;
    uint32_t resets;
    uint8_t programmed_state;
    uint8_t current_state;
} QEMU_PACKED;

/*
 * Security message layouts for K3 SA2UL, OTP and secure-boot services. They
 * match the U-Boot and Zephyr TI-SCI protocol headers.
 */
#define FWL_MAX_PRIVID_SLOTS 3U

struct TisciMsgReqFwlSetFirewallRegion {
    TISciMsgHdr hdr;
    uint16_t fwl_id;
    uint16_t region;
    uint32_t n_permission_regs;
    uint32_t control;
    uint32_t permissions[FWL_MAX_PRIVID_SLOTS];
    uint64_t start_address;
    uint64_t end_address;
} QEMU_PACKED;

/* TISCI_MSG_FWL_SET response is bare generic ACK/NACK (TISciMsgHdr). */

struct TisciMsgReqFwlGetFirewallRegion {
    TISciMsgHdr hdr;
    uint16_t fwl_id;
    uint16_t region;
    uint32_t n_permission_regs;
} QEMU_PACKED;

struct TisciMsgRespFwlGetFirewallRegion {
    TISciMsgHdr hdr;
    uint16_t fwl_id;
    uint16_t region;
    uint32_t n_permission_regs;
    uint32_t control;
    uint32_t permissions[FWL_MAX_PRIVID_SLOTS];
    uint64_t start_address;
    uint64_t end_address;
} QEMU_PACKED;

struct TisciMsgReqFwlChangeOwnerInfo {
    TISciMsgHdr hdr;
    uint16_t fwl_id;
    uint16_t region;
    uint8_t owner_index;
} QEMU_PACKED;

struct TisciMsgRespFwlChangeOwnerInfo {
    TISciMsgHdr hdr;
    uint16_t fwl_id;
    uint16_t region;
    uint8_t owner_index;
    uint8_t owner_privid;
    uint16_t owner_permission_bits;
} QEMU_PACKED;

#define SA2UL_DKEK_KEY_LEN 32
#define KDF_LABEL_AND_CONTEXT_LEN_MAX 41

struct TisciMsgReqSa2ulGetDkek {
    TISciMsgHdr hdr;
    uint8_t sa2ul_instance;
    uint8_t kdf_label_len;
    uint8_t kdf_context_len;
    uint8_t kdf_label_and_context[KDF_LABEL_AND_CONTEXT_LEN_MAX];
} QEMU_PACKED;

struct TisciMsgRespSa2ulGetDkek {
    TISciMsgHdr hdr;
    uint8_t dkek[SA2UL_DKEK_KEY_LEN];
} QEMU_PACKED;

struct TisciMsgRespReadSwrev {
    TISciMsgHdr hdr;
    uint32_t swrev;
} QEMU_PACKED;

struct TisciMsgRespReadKeycntKeyrev {
    TISciMsgHdr hdr;
    uint32_t keycnt;
    uint32_t keyrev;
} QEMU_PACKED;

typedef struct TIDmscClient TIDmscClient;

typedef void (*TiDmscMsgHandler)(TIDmscClient *client, TISciMsgHdr *hdr,
                                 uint16_t thread_id, const uint32_t *words,
                                 size_t nwords);

struct TIDmscClient {
    TIDmscState *dmsc;
    uint16_t rx_thread_id;
    uint16_t tx_thread_id;
    bool pending;
    /*
     * Secure R5 clients add a 4-byte checksum/reserved word before the
     * normal TISciMsgHdr on requests and responses.
     */
    bool secure;
    uint32_t pending_words[TI_DMSC_MAX_WORDS];
    size_t pending_nwords;

    /*
     * Set from the actual request's AOP bit before dispatch. The bottom
     * half handles one message at a time, so no extra locking is needed.
     */
    bool cur_req_wants_resp;
};

struct TIDmscState {
    DeviceState parent_obj;

    /* QOM link to SEC_PROXY */
    TISecProxyState *sec_proxy;

    /* Config */
    uint16_t rx_thread_id; /* e.g. M4_0_WRITE_THREAD */
    uint16_t tx_thread_id; /* e.g. M4_0_READ_RESPONSE_THREAD */
    uint32_t num_rx_threads;
    uint16_t *rx_thread_ids;
    uint32_t num_tx_threads;
    uint16_t *tx_thread_ids;
    /* rx threads of clients, which use secure R5 transport framing */
    uint32_t num_secure_rx_threads;
    uint16_t *secure_rx_threads;
    uint64_t m4_cpu_id;       /* QEMU CPU index used for MCU M4 */
    uint64_t a53_cpu_id_base; /* MP affinity of A53 core 0 (core 1 = +1) */

    uint32_t msg_words; /* usually 16 */

    /* Optional async handling */
    QEMUBH *bh;
    QemuMutex lock;

    TiDmscMsgHandler msg_handler[TISCI_MSG_MAX_ID];
    uint32_t num_clients;
    TIDmscClient *clients;

    uint8_t dev_hw_state[TISCI_DEV_ID_MAX];
    uint8_t dev_prog_state[TISCI_DEV_ID_MAX];
    bool m4_running;
    /* A53 boot vectors captured from TISCI_MSG_SET_CONFIG. */
    uint64_t proc_bootvector[2];
};

#endif /* HW_MISC_TI_DMSC_H */
