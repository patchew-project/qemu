/*
 * TI DMSC emulator (minimal TISCI service) as QOM device
 *
 * This is used together with a TI SEC_PROXY model. The SEC_PROXY stays the
 * transport/queue/data-window part, this device handles SYSFW/TISCI logic.
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/core/resettable.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "target/arm/arm-powerctl.h"
#include "qemu/main-loop.h"
#include "hw/misc/ti-dmsc.h"
#include "trace.h"

/*
 * Responses, which go through sec-proxy, have one word less payload than the
 * slot size: word 0 is reserved by ti_sec_proxy_push_msg().
 * TI_DMSC_MAX_WORDS is the TISCI request limit, not transport capacity.
 */
#define TI_DMSC_SEC_PROXY_PAYLOAD_MAX \
    ((SEC_PROXY_MSG_MAX_WORDS - 1) * sizeof(uint32_t))

static const char *ti_dmsc_proc_name_from_id(uint32_t proc_id)
{
    switch (proc_id) {
    case SCICLIENT_PROCID_A53_CL0_C0:
        return "A53_CL0_C0";
    case SCICLIENT_PROCID_A53_CL0_C1:
        return "A53_CL0_C1";

    case SCICLIENT_PROCID_R5_CL0_C0:
        return "R5_CL0_C0";
    case SCICLIENT_PROCID_R5_CL0_C1:
        return "R5_CL0_C1";
    case SCICLIENT_PROCID_R5_CL1_C0:
        return "R5_CL1_C0";
    case SCICLIENT_PROCID_R5_CL1_C1:
        return "R5_CL1_C1";

    case SCICLIENT_PROCID_MCU_M4FSS0_C0:
        return "MCU_M4FSS0_C0";

    default:
        return "UNKNOWN_PROC";
    }
}

static const char *ti_dmsc_host_name_from_id(uint32_t host_id)
{
    switch (host_id) {
    case TISCI_HOST_ID_DMSC:
        return "DMSC";

    case TISCI_HOST_ID_MAIN_0_R5_0:
        return "MAIN_0_R5_0";
    case TISCI_HOST_ID_MAIN_0_R5_1:
        return "MAIN_0_R5_1";
    case TISCI_HOST_ID_MAIN_0_R5_2:
        return "MAIN_0_R5_2";
    case TISCI_HOST_ID_MAIN_0_R5_3:
        return "MAIN_0_R5_3";

    case TISCI_HOST_ID_A53_0:
        return "A53_0";
    case TISCI_HOST_ID_A53_1:
        return "A53_1";
    case TISCI_HOST_ID_A53_2:
        return "A53_2";
    case TISCI_HOST_ID_A53_3:
        return "A53_3";
    case TISCI_HOST_ID_A53_4:
        return "A53_4";

    case TISCI_HOST_ID_M4_0:
        return "M4_0";

    case TISCI_HOST_ID_MAIN_1_R5_0:
        return "MAIN_1_R5_0";
    case TISCI_HOST_ID_MAIN_1_R5_1:
        return "MAIN_1_R5_1";
    case TISCI_HOST_ID_MAIN_1_R5_2:
        return "MAIN_1_R5_2";
    case TISCI_HOST_ID_MAIN_1_R5_3:
        return "MAIN_1_R5_3";

    case TISCI_HOST_ID_ICSSG_0:
        return "ICSSG_0";
    case TISCI_HOST_ID_ICSSG_1:
        return "ICSSG_1";

    default:
        return "UNKNOWN_HOST";
    }
}

static const char *ti_dmsc_device_state_to_str(uint8_t state)
{
    switch (state) {
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_AUTO_OFF:
        return "AUTO";
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_RETENTION:
        return "RETENTION";
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_ON:
        return "ON";
    default:
        return "UNKNOWN_STATE";
    }
}

static const char *ti_dmsc_device_name_from_id(uint32_t dev_id)
{
    switch (dev_id) {
    case TISCI_DEV_ADC0:
        return "ADC0";
    case TISCI_DEV_CMP_EVENT_INTROUTER0:
        return "CMP_EVENT_INTROUTER0";
    case TISCI_DEV_DBGSUSPENDROUTER0:
        return "DBGSUSPENDROUTER0";
    case TISCI_DEV_MAIN_GPIOMUX_INTROUTER0:
        return "MAIN_GPIOMUX_INTROUTER0";
    case TISCI_DEV_MCU_MCU_GPIOMUX_INTROUTER0:
        return "MCU_MCU_GPIOMUX_INTROUTER0";
    case TISCI_DEV_TIMESYNC_EVENT_INTROUTER0:
        return "TIMESYNC_EVENT_INTROUTER0";
    case TISCI_DEV_MCU_M4FSS0:
        return "MCU_M4FSS0";
    case TISCI_DEV_MCU_M4FSS0_CBASS_0:
        return "MCU_M4FSS0_CBASS_0";
    case TISCI_DEV_MCU_M4FSS0_CORE0:
        return "MCU_M4FSS0_CORE0";
    case TISCI_DEV_CPSW0:
        return "CPSW0";
    case TISCI_DEV_CPT2_AGGR0:
        return "CPT2_AGGR0";
    case TISCI_DEV_STM0:
        return "STM0";
    case TISCI_DEV_DCC0:
        return "DCC0";
    case TISCI_DEV_DCC1:
        return "DCC1";
    case TISCI_DEV_DCC2:
        return "DCC2";
    case TISCI_DEV_DCC3:
        return "DCC3";
    case TISCI_DEV_DCC4:
        return "DCC4";
    case TISCI_DEV_DCC5:
        return "DCC5";
    case TISCI_DEV_DMSC0:
        return "DMSC0";
    case TISCI_DEV_MCU_DCC0:
        return "MCU_DCC0";
    case TISCI_DEV_DEBUGSS_WRAP0:
        return "DEBUGSS_WRAP0";
    case TISCI_DEV_DMASS0:
        return "DMASS0";
    case TISCI_DEV_DMASS0_BCDMA_0:
        return "DMASS0_BCDMA_0";
    case TISCI_DEV_DMASS0_CBASS_0:
        return "DMASS0_CBASS_0";
    case TISCI_DEV_DMASS0_INTAGGR_0:
        return "DMASS0_INTAGGR_0";
    case TISCI_DEV_DMASS0_IPCSS_0:
        return "DMASS0_IPCSS_0";
    case TISCI_DEV_DMASS0_PKTDMA_0:
        return "DMASS0_PKTDMA_0";
    case TISCI_DEV_DMASS0_RINGACC_0:
        return "DMASS0_RINGACC_0";
    case TISCI_DEV_MCU_TIMER0:
        return "MCU_TIMER0";
    case TISCI_DEV_TIMER0:
        return "TIMER0";
    case TISCI_DEV_TIMER1:
        return "TIMER1";
    case TISCI_DEV_TIMER2:
        return "TIMER2";
    case TISCI_DEV_TIMER3:
        return "TIMER3";
    case TISCI_DEV_TIMER4:
        return "TIMER4";
    case TISCI_DEV_TIMER5:
        return "TIMER5";
    case TISCI_DEV_TIMER6:
        return "TIMER6";
    case TISCI_DEV_TIMER7:
        return "TIMER7";
    case TISCI_DEV_TIMER8:
        return "TIMER8";
    case TISCI_DEV_TIMER9:
        return "TIMER9";
    case TISCI_DEV_TIMER10:
        return "TIMER10";
    case TISCI_DEV_TIMER11:
        return "TIMER11";
    case TISCI_DEV_MCU_TIMER1:
        return "MCU_TIMER1";
    case TISCI_DEV_MCU_TIMER2:
        return "MCU_TIMER2";
    case TISCI_DEV_MCU_TIMER3:
        return "MCU_TIMER3";
    case TISCI_DEV_ECAP0:
        return "ECAP0";
    case TISCI_DEV_ECAP1:
        return "ECAP1";
    case TISCI_DEV_ECAP2:
        return "ECAP2";
    case TISCI_DEV_ELM0:
        return "ELM0";
    case TISCI_DEV_EMIF_DATA_0_VD:
        return "EMIF_DATA_0_VD";
    case TISCI_DEV_MMCSD0:
        return "MMCSD0";
    case TISCI_DEV_MMCSD1:
        return "MMCSD1";
    case TISCI_DEV_EQEP0:
        return "EQEP0";
    case TISCI_DEV_EQEP1:
        return "EQEP1";
    case TISCI_DEV_GTC0:
        return "GTC0";
    case TISCI_DEV_EQEP2:
        return "EQEP2";
    case TISCI_DEV_ESM0:
        return "ESM0";
    case TISCI_DEV_MCU_ESM0:
        return "MCU_ESM0";
    case TISCI_DEV_FSIRX0:
        return "FSIRX0";
    case TISCI_DEV_FSIRX1:
        return "FSIRX1";
    case TISCI_DEV_FSIRX2:
        return "FSIRX2";
    case TISCI_DEV_FSIRX3:
        return "FSIRX3";
    case TISCI_DEV_FSIRX4:
        return "FSIRX4";
    case TISCI_DEV_FSIRX5:
        return "FSIRX5";
    case TISCI_DEV_FSITX0:
        return "FSITX0";
    case TISCI_DEV_FSITX1:
        return "FSITX1";
    case TISCI_DEV_FSS0:
        return "FSS0";
    case TISCI_DEV_FSS0_FSAS_0:
        return "FSS0_FSAS_0";
    case TISCI_DEV_FSS0_OSPI_0:
        return "FSS0_OSPI_0";
    case TISCI_DEV_GICSS0:
        return "GICSS0";
    case TISCI_DEV_GPIO0:
        return "GPIO0";
    case TISCI_DEV_GPIO1:
        return "GPIO1";
    case TISCI_DEV_MCU_GPIO0:
        return "MCU_GPIO0";
    case TISCI_DEV_GPMC0:
        return "GPMC0";
    case TISCI_DEV_PRU_ICSSG0:
        return "PRU_ICSSG0";
    case TISCI_DEV_PRU_ICSSG1:
        return "PRU_ICSSG1";
    case TISCI_DEV_LED0:
        return "LED0";
    case TISCI_DEV_CPTS0:
        return "CPTS0";
    case TISCI_DEV_DDPA0:
        return "DDPA0";
    case TISCI_DEV_EPWM0:
        return "EPWM0";
    case TISCI_DEV_EPWM1:
        return "EPWM1";
    case TISCI_DEV_EPWM2:
        return "EPWM2";
    case TISCI_DEV_EPWM3:
        return "EPWM3";
    case TISCI_DEV_EPWM4:
        return "EPWM4";
    case TISCI_DEV_EPWM5:
        return "EPWM5";
    case TISCI_DEV_EPWM6:
        return "EPWM6";
    case TISCI_DEV_EPWM7:
        return "EPWM7";
    case TISCI_DEV_EPWM8:
        return "EPWM8";
    case TISCI_DEV_VTM0:
        return "VTM0";
    case TISCI_DEV_MAILBOX0:
        return "MAILBOX0";
    case TISCI_DEV_MAIN2MCU_VD:
        return "MAIN2MCU_VD";
    case TISCI_DEV_MCAN0:
        return "MCAN0";
    case TISCI_DEV_MCAN1:
        return "MCAN1";
    case TISCI_DEV_MCU_MCRC64_0:
        return "MCU_MCRC64_0";
    case TISCI_DEV_MCU2MAIN_VD:
        return "MCU2MAIN_VD";
    case TISCI_DEV_I2C0:
        return "I2C0";
    case TISCI_DEV_I2C1:
        return "I2C1";
    case TISCI_DEV_I2C2:
        return "I2C2";
    case TISCI_DEV_I2C3:
        return "I2C3";
    case TISCI_DEV_MCU_I2C0:
        return "MCU_I2C0";
    case TISCI_DEV_MCU_I2C1:
        return "MCU_I2C1";
    case TISCI_DEV_PCIE0:
        return "PCIE0";
    case TISCI_DEV_R5FSS0:
        return "R5FSS0";
    case TISCI_DEV_R5FSS1:
        return "R5FSS1";
    case TISCI_DEV_R5FSS0_CORE0:
        return "R5FSS0_CORE0";
    case TISCI_DEV_R5FSS0_CORE1:
        return "R5FSS0_CORE1";
    case TISCI_DEV_R5FSS1_CORE0:
        return "R5FSS1_CORE0";
    case TISCI_DEV_R5FSS1_CORE1:
        return "R5FSS1_CORE1";
    case TISCI_DEV_RTI0:
        return "RTI0";
    case TISCI_DEV_RTI1:
        return "RTI1";
    case TISCI_DEV_RTI8:
        return "RTI8";
    case TISCI_DEV_RTI9:
        return "RTI9";
    case TISCI_DEV_RTI10:
        return "RTI10";
    case TISCI_DEV_RTI11:
        return "RTI11";
    case TISCI_DEV_MCU_RTI0:
        return "MCU_RTI0";
    case TISCI_DEV_SA2_UL0:
        return "SA2_UL0";
    case TISCI_DEV_COMPUTE_CLUSTER0:
        return "COMPUTE_CLUSTER0";
    case TISCI_DEV_A53SS0_CORE_0:
        return "A53SS0_CORE_0";
    case TISCI_DEV_A53SS0_CORE_1:
        return "A53SS0_CORE_1";
    case TISCI_DEV_A53SS0:
        return "A53SS0";
    case TISCI_DEV_DDR16SS0:
        return "DDR16SS0";
    case TISCI_DEV_PSC0:
        return "PSC0";
    case TISCI_DEV_MCU_PSC0:
        return "MCU_PSC0";
    case TISCI_DEV_MCSPI0:
        return "MCSPI0";
    case TISCI_DEV_MCSPI1:
        return "MCSPI1";
    case TISCI_DEV_MCSPI2:
        return "MCSPI2";
    case TISCI_DEV_MCSPI3:
        return "MCSPI3";
    case TISCI_DEV_MCSPI4:
        return "MCSPI4";
    case TISCI_DEV_UART0:
        return "UART0";
    case TISCI_DEV_MCU_MCSPI0:
        return "MCU_MCSPI0";
    case TISCI_DEV_MCU_MCSPI1:
        return "MCU_MCSPI1";
    case TISCI_DEV_MCU_UART0:
        return "MCU_UART0";
    case TISCI_DEV_SPINLOCK0:
        return "SPINLOCK0";
    case TISCI_DEV_TIMERMGR0:
        return "TIMERMGR0";
    case TISCI_DEV_UART1:
        return "UART1";
    case TISCI_DEV_UART2:
        return "UART2";
    case TISCI_DEV_UART3:
        return "UART3";
    case TISCI_DEV_UART4:
        return "UART4";
    case TISCI_DEV_UART5:
        return "UART5";
    case TISCI_DEV_BOARD0:
        return "BOARD0";
    case TISCI_DEV_UART6:
        return "UART6";
    case TISCI_DEV_MCU_UART1:
        return "MCU_UART1";
    case TISCI_DEV_USB0:
        return "USB0";
    case TISCI_DEV_SERDES_10G0:
        return "SERDES_10G0";
    case TISCI_DEV_PBIST0:
        return "PBIST0";
    case TISCI_DEV_PBIST1:
        return "PBIST1";
    case TISCI_DEV_PBIST2:
        return "PBIST2";
    case TISCI_DEV_PBIST3:
        return "PBIST3";
    case TISCI_DEV_COMPUTE_CLUSTER0_PBIST_0:
        return "COMPUTE_CLUSTER0_PBIST_0";
    default:
        return "UNKNOWN";
    }
}

static void ti_dmsc_init_device_states(TIDmscState *s)
{
    for (size_t i = 0; i < TISCI_DEV_ID_MAX; i++) {
        s->dev_hw_state[i] = TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
        s->dev_prog_state[i] = TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
    }

    s->dev_hw_state[TISCI_DEV_MCU_M4FSS0_CORE0] =
        TISCI_MSG_VALUE_DEVICE_HW_STATE_OFF;
    s->dev_prog_state[TISCI_DEV_MCU_M4FSS0_CORE0] =
        TISCI_MSG_VALUE_DEVICE_HW_STATE_OFF;
    s->m4_running = false;
    memset(s->proc_bootvector, 0, sizeof(s->proc_bootvector));
}

static const char *ti_dmsc_message_name_from_id(uint16_t msg_id)
{
    switch (msg_id) {
    case TISCI_MSG_GET_DEVICE:
        return "GET_DEVICE";
    case TISCI_MSG_SET_DEVICE:
        return "SET_DEVICE";
    case TISCI_MSG_SET_DEVICE_RESETS:
        return "SET_DEVICE_RESETS";
    case TISCI_MSG_DEVICE_DROP_POWERUP_REF:
        return "DEVICE_DROP_POWERUP_REF";
    case TISCI_MSG_PREPARE_SLEEP:
        return "PREPARE_SLEEP";
    case TISCI_MSG_ENTER_SLEEP:
        return "ENTER_SLEEP";
    case TISCI_MSG_VERSION:
        return "VERSION";
    case TISCI_MSG_BOOT_NOTIFICATION:
        return "BOOT_NOTIFICATION";
    case TISCI_MSG_BOARD_CONFIG:
        return "BOARD_CONFIG";
    case TISCI_MSG_BOARD_CONFIG_RM:
        return "BOARD_CONFIG_RM";
    case TISCI_MSG_BOARD_CONFIG_SECURITY:
        return "BOARD_CONFIG_SECURITY";
    case TISCI_MSG_BOARD_CONFIG_PM:
        return "BOARD_CONFIG_PM";
    case TISCI_MSG_ENABLE_WDT:
        return "ENABLE_WDT";
    case TISCI_MSG_WAKE_RESET:
        return "WAKE_RESET";
    case TISCI_MSG_WAKE_REASON:
        return "WAKE_REASON";
    case TISCI_MSG_GOODBYE:
        return "GOODBYE";
    case TISCI_MSG_SYS_RESET:
        return "SYS_RESET";
    case TISCI_MSG_QUERY_MSMC:
        return "QUERY_MSMC";
    case TISCI_MSG_GET_TRACE_CONFIG:
        return "GET_TRACE_CONFIG";
    case TISCI_MSG_QUERY_FW_CAPS:
        return "QUERY_FW_CAPS";
    case TISCI_MSG_SET_CLOCK:
        return "SET_CLOCK";
    case TISCI_MSG_GET_CLOCK:
        return "GET_CLOCK";
    case TISCI_MSG_SET_CLOCK_PARENT:
        return "SET_CLOCK_PARENT";
    case TISCI_MSG_GET_CLOCK_PARENT:
        return "GET_CLOCK_PARENT";
    case TISCI_MSG_GET_NUM_CLOCK_PARENTS:
        return "GET_NUM_CLOCK_PARENTS";
    case TISCI_MSG_SET_FREQ:
        return "SET_FREQ";
    case TISCI_MSG_QUERY_FREQ:
        return "QUERY_FREQ";
    case TISCI_MSG_GET_FREQ:
        return "GET_FREQ";
    case TISCI_MSG_PROC_REQUEST:
        return "PROC_REQUEST";
    case TISCI_MSG_PROC_RELEASE:
        return "PROC_RELEASE";
    case TISCI_MSG_PROC_HANDOVER:
        return "PROC_HANDOVER";
    case TISCI_MSG_SET_CONFIG:
        return "SET_CONFIG";
    case TISCI_MSG_SET_CTRL:
        return "SET_CTRL";
    case TISCI_MSG_GET_STATUS:
        return "GET_STATUS";
    case TISCI_MSG_WAIT_PROC_BOOT_STATUS:
        return "WAIT_PROC_BOOT_STATUS";
    case TISCI_MSG_FWL_SET:
        return "FWL_SET";
    case TISCI_MSG_FWL_GET:
        return "FWL_GET";
    case TISCI_MSG_FWL_CHANGE_OWNER:
        return "FWL_CHANGE_OWNER";
    case TISCI_MSG_SA2UL_GET_DKEK:
        return "SA2UL_GET_DKEK";
    case TISCI_MSG_READ_SWREV:
        return "READ_SWREV";
    case TISCI_MSG_READ_KEYCNT_KEYREV:
        return "READ_KEYCNT_KEYREV";
    default:
        return "UNKNOWN";
    }
}

/*
 * The DMSC has no MMIO. Sec-proxy calls us, when a client commits the last
 * word of a request data window.
 */
static void ti_dmsc_handle_one(TIDmscClient *client, uint16_t thread_id,
                               const uint32_t *words, size_t nwords);

/*
 * Push a response to the client's TX thread. Secure hosts carry a 4-byte
 * {u16 checksum; u16 reserved} prefix before TISciMsgHdr. The checksum word
 * is present in the transport, but not validated by this model.
 *
 * TI_SCI_FLAG_REQ_GENERIC_NORESPONSE is handled here too. The handlers run
 * still for side effects, but replies are suppressed unless AOP was set.
 */
static size_t ti_dmsc_client_respond(TIDmscClient *client, const void *msg,
                                     size_t nbytes)
{
    TIDmscState *s = client->dmsc;
    const uint32_t *words = msg;

    if (!client->cur_req_wants_resp) {
        return nbytes;
    }

    if (client->secure) {
        uint32_t buf[TI_DMSC_MAX_WORDS + 1] = {0};

        /*
         * Control the sec-proxy payload limit, not only the local scratch
         * buffer. The scratch buffer has the extra secure-prefix word.
         */
        if (nbytes + sizeof(uint32_t) > TI_DMSC_SEC_PROXY_PAYLOAD_MAX) {
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "ti-dmsc: secure response too large (%zu bytes), dropping\n",
                nbytes);
            return 0;
        }

        memcpy((uint8_t *)buf + sizeof(uint32_t), words, nbytes);
        return ti_sec_proxy_push_msg(s->sec_proxy, client->tx_thread_id, buf,
                                     nbytes + sizeof(uint32_t));
    }

    if (nbytes > TI_DMSC_SEC_PROXY_PAYLOAD_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: response too large (%zu bytes), dropping\n",
                      nbytes);
        return 0;
    }

    return ti_sec_proxy_push_msg(s->sec_proxy, client->tx_thread_id, words,
                                 nbytes);
}

/*
 * Queue TISCI_MSG_BOOT_NOTIFICATION for secure boot hosts. SYSFW sends this
 * unsolicited message once it is ready on the host response thread.
 *
 * Re-arm it on every reset: sec-proxy reset leaves thread slots alone. Reset
 * the outbound counter first, since pushes only increment it.
 */
static void ti_dmsc_send_boot_notification(TIDmscClient *client)
{
    TISciMsgHdr notif = {0};

    notif.type = TISCI_MSG_BOOT_NOTIFICATION;
    notif.host = TISCI_HOST_ID_DMSC;
    notif.seq = 0;
    notif.flags = 0;

    ti_sec_proxy_reset_thread_count(client->dmsc->sec_proxy,
                                    client->tx_thread_id);

    /*
     * This is unsolicited, so ignore no-response state of the previous
     * request and force delivery.
     */
    client->cur_req_wants_resp = true;

    if (!ti_dmsc_client_respond(client, &notif, sizeof(notif))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push BOOT_NOTIFICATION into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/* Bottom half: handle pending message outside of MMIO context */
static void ti_dmsc_bh(void *opaque)
{
    TIDmscState *s = opaque;
    uint32_t local_words[TI_DMSC_MAX_WORDS];

    while (true) {
        TIDmscClient *client = NULL;
        size_t local_nwords = 0;
        uint16_t tid = 0;

        qemu_mutex_lock(&s->lock);
        for (uint32_t i = 0; i < s->num_clients; i++) {
            if (s->clients[i].pending) {
                client = &s->clients[i];
                break;
            }
        }
        if (!client) {
            qemu_mutex_unlock(&s->lock);
            return;
        }

        tid = client->rx_thread_id;
        local_nwords = client->pending_nwords;
        if (local_nwords > TI_DMSC_MAX_WORDS) {
            local_nwords = TI_DMSC_MAX_WORDS;
        }
        memcpy(local_words, client->pending_words,
               local_nwords * sizeof(uint32_t));
        client->pending = false;
        client->pending_nwords = 0;
        qemu_mutex_unlock(&s->lock);

        ti_dmsc_handle_one(client, tid, local_words, local_nwords);
    }
}

/*
 * Sec-proxy calls this, when a client commits a message on our RX thread.
 */
static void ti_dmsc_sec_proxy_cb(void *opaque, uint16_t thread_id,
                                 const uint32_t *words, size_t nwords)
{
    TIDmscClient *client = opaque;
    TIDmscState *s = client->dmsc;

    if (thread_id != client->rx_thread_id) {
        return;
    }

    qemu_mutex_lock(&s->lock);
    client->pending = true;

    if (nwords > TI_DMSC_MAX_WORDS) {
        nwords = TI_DMSC_MAX_WORDS;
    }
    memcpy(client->pending_words, words, nwords * sizeof(uint32_t));
    client->pending_nwords = nwords;
    qemu_mutex_unlock(&s->lock);

    qemu_bh_schedule(s->bh);
}

/* Dispatch one committed TISCI message. */
static void ti_dmsc_handle_one(TIDmscClient *client, uint16_t thread_id,
                               const uint32_t *words, size_t nwords)
{
    TIDmscState *s = client->dmsc;

    if (!s->sec_proxy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: No sec-proxy linked, dropping message\n");
        return;
    }

    size_t hdr_words = sizeof(TISciMsgHdr) / sizeof(uint32_t);
    size_t min_words = hdr_words + (client->secure ? 1 : 0);

    if (nwords < min_words) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Short message (words=%zu), dropping\n", nwords);
        return;
    }

    if (client->secure) {
        /*
         * Secure R5 clients prepend a checksum/reserved word before
         * TISciMsgHdr. Drop it before we cast payloads.
         */
        words += 1;
        nwords -= 1;
    }

    TISciMsgHdr hdr = {0};
    memcpy(&hdr, words, MIN(sizeof(hdr), nwords * sizeof(uint32_t)));

    /*
     * Remember AOP before dispatch. ti_dmsc_client_respond() uses it to
     * suppress replies for requests with TI_SCI_FLAG_REQ_GENERIC_NORESPONSE.
     */
    client->cur_req_wants_resp = (hdr.flags & TISCI_MSG_FLAG_AOP) != 0;

    if (hdr.type < ARRAY_SIZE(s->msg_handler) && s->msg_handler[hdr.type]) {
        trace_dmsc_new_message_received(
            hdr.type, ti_dmsc_message_name_from_id(hdr.type),
            ti_dmsc_host_name_from_id(hdr.host), thread_id);
        s->msg_handler[hdr.type](client, &hdr, thread_id, words, nwords);
        return;
    } else {
        TISciMsgHdr resp = hdr;

        trace_dmsc_unsupported_message(
            ti_dmsc_message_name_from_id(hdr.type), hdr.type,
            ti_dmsc_host_name_from_id(hdr.host), thread_id);
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "ti-dmsc: No handler for message type=0x%04x (%s), dropping\n",
            hdr.type, ti_dmsc_message_name_from_id(hdr.type));

        /*
         * Header-only NAKs unblock callers, which wait for a response,
         * instead of leaving them to time out.
         */
        resp.flags = 0;
        if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ti-dmsc: Failed to push NAK response into sec-proxy "
                          "thread=%u\n",
                          client->tx_thread_id);
        }
    }
}

static void ti_dmsc_reset_hold(Object *obj, ResetType type)
{
    TIDmscState *s = TI_DMSC(obj);

    qemu_mutex_lock(&s->lock);
    ti_dmsc_init_device_states(s);
    for (uint32_t i = 0; i < s->num_clients; i++) {
        s->clients[i].pending = false;
        s->clients[i].pending_nwords = 0;
        memset(s->clients[i].pending_words, 0,
               sizeof(s->clients[i].pending_words));
    }
    /*
     * msg_handler contains realize-time function pointers, not guest state.
     * Clearing it during reset disables all TISCI handlers after the first
     * cold reset.
     */
    qemu_mutex_unlock(&s->lock);

    /*
     * Secure clients get a fresh boot notification after every reset.
     */
    for (uint32_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].secure) {
            ti_dmsc_send_boot_notification(&s->clients[i]);
        }
    }
}

static TISciMsgHdr ti_dmsc_set_resp_flags(TISciMsgHdr *req_hdr, int add_flags)
{
    TISciMsgHdr resp = *req_hdr;
    resp.flags =
        ((req_hdr->flags & TISCI_MSG_FLAG_AOP) ? TISCI_MSG_FLAG_ACK : 0) |
        add_flags;
    return resp;
}

static void ti_dmsc_handle_set_clock(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    struct TisciMsgSetClockReq *req = (struct TisciMsgSetClockReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_clock(ti_dmsc_message_name_from_id(hdr->type),
                                ti_dmsc_host_name_from_id(hdr->host),
                                ti_dmsc_device_name_from_id(req->device),
                                req->clk);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CLOCK response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * SET_CLOCK_PARENT stores no clock tree state. The model only returns the
 * generic TISCI ACK/NAK header.
 */
static void ti_dmsc_handle_set_clock_parent(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    struct TisciMsgSetClockParentReq *req =
        (struct TisciMsgSetClockParentReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_clock_parent(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       ti_dmsc_device_name_from_id(req->dev_id),
                                       req->clk_id, req->parent_id);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CLOCK_PARENT response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                    uint16_t thread_id, const uint32_t *words,
                                    size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_FREQ response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_query_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                      uint16_t thread_id, const uint32_t *words,
                                      size_t nwords)
{
    struct TisciMsgQueryFreqReq *req = (struct TisciMsgQueryFreqReq *)words;
    struct TisciMsgQueryFreqResp resp = {0};

    trace_dmsc_handle_query_freq(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host),
                                 ti_dmsc_device_name_from_id(req->device),
                                 req->clk, req->clk32, req->target_freq_hz);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.freq_hz = req->target_freq_hz;

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push QUERY_FREQ response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                    uint16_t thread_id, const uint32_t *words,
                                    size_t nwords)
{
    struct TisciMsgGetFreqReq *req = (struct TisciMsgGetFreqReq *)words;
    struct TisciMsgQueryFreqResp resp = {0};

    trace_dmsc_handle_get_freq(ti_dmsc_message_name_from_id(hdr->type),
                               ti_dmsc_host_name_from_id(hdr->host),
                               ti_dmsc_device_name_from_id(req->device),
                               req->clk);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    /*
     * Clock rates are not modelled per device. Return the fixed 200 MHz
     * rate used by the AM64x board model for generic clocks.
     */
    resp.freq_hz = 200000000ULL;

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_FREQ response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_clock_parents(TIDmscClient *client,
                                             TISciMsgHdr *hdr,
                                             uint16_t thread_id,
                                             const uint32_t *words,
                                             size_t nwords)
{
    struct TisciMsgGetNumClockParentsReq *req =
        (struct TisciMsgGetNumClockParentsReq *)words;
    struct TisciMsgGetNumClockParentsResp resp = {0};

    trace_dmsc_handle_get_clock_parents(
        ti_dmsc_message_name_from_id(hdr->type),
        ti_dmsc_host_name_from_id(hdr->host),
        ti_dmsc_device_name_from_id(req->device), req->clk, req->clk32);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    /*
     * Clock parent topology is not modelled. Report two possible parents,
     * which is the minimum topology where SET_CLOCK_PARENT has a meaning.
     */
    resp.num_parents = 2;
    resp.num_parentint32_t = UINT_MAX;

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_CLOCK_PARENTS response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * Clock muxes are not modelled. Report parent 0 as actual parent for all
 * clocks.
 */
static void ti_dmsc_handle_get_clock_parent(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    struct TisciMsgGetClockParentResp resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.parent = 0;
    resp.parent32 = 0;

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_CLOCK_PARENT response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_clock(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    struct TisciMsgGetClockReq *req = (struct TisciMsgGetClockReq *)words;
    struct TisciMsgGetClockResp resp = {0};

    trace_dmsc_handle_get_clock(ti_dmsc_message_name_from_id(hdr->type),
                                ti_dmsc_host_name_from_id(hdr->host),
                                ti_dmsc_device_name_from_id(req->device),
                                req->clk, req->clk32);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.current_state = resp.programmed_state =
        TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_CLOCK response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_stop_proc(TIDmscClient *client, TISciMsgHdr *hdr,
                              uint16_t thread_id, const uint32_t *words,
                              size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TiSciMsgReqProcRelease *req = (struct TiSciMsgReqProcRelease *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);
    trace_dmsc_stop_proc(ti_dmsc_proc_name_from_id(req->processor_id),
                         req->processor_id,
                         ti_dmsc_host_name_from_id(hdr->host));

    if (req->processor_id == SCICLIENT_PROCID_MCU_M4FSS0_C0) {
        s->m4_running = false;
    }

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_RELEASE response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_start_proc(TIDmscClient *client, TISciMsgHdr *hdr,
                               uint16_t thread_id, const uint32_t *words,
                               size_t nwords)
{
    struct TiSciMsgReqProcRequest *req = (struct TiSciMsgReqProcRequest *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_start_proc(ti_dmsc_proc_name_from_id(req->processor_id),
                          req->processor_id,
                          ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_REQUEST response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * SET_CTRL carries processor control flags, e.g. halt and reset-vector
 * latch. Boot vectors are captured by SET_CONFIG and release happens via
 * SET_DEVICE, so here is no processor-control state modeled.
 */
static void ti_dmsc_handle_proc_set_ctrl(TIDmscClient *client, TISciMsgHdr *hdr,
                                         uint16_t thread_id,
                                         const uint32_t *words, size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CTRL response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * PROC_HANDOVER transfers a processor to another TISCI host. Ownership is
 * not modeled; the request gets only the generic ACK/NAK header.
 */
static void ti_dmsc_handover_proc(TIDmscClient *client, TISciMsgHdr *hdr,
                                  uint16_t thread_id, const uint32_t *words,
                                  size_t nwords)
{
    struct TiSciMsgReqProcHandover *req =
        (struct TiSciMsgReqProcHandover *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handover_proc(ti_dmsc_proc_name_from_id(req->processor_id),
                             req->processor_id,
                             ti_dmsc_host_name_from_id(req->host_id),
                             ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_HANDOVER response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * SYS_RESET is a TISCI system-reset command and is normally sent without
 * AOP. Request a QEMU system reset; an AOP caller gets the bare ACK first.
 */
static void ti_dmsc_handle_sys_reset(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_sys_reset(ti_dmsc_message_name_from_id(hdr->type),
                                ti_dmsc_host_name_from_id(hdr->host));

    ti_dmsc_client_respond(client, &resp, sizeof(resp));
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

static void ti_dmsc_query_hw_caps(TIDmscClient *client, TISciMsgHdr *hdr,
                                  uint16_t thread_id, const uint32_t *words,
                                  size_t nwords)
{
    struct TiSciMsgQueryFwCapsResp resp = {0};
    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fw_caps = MSG_FLAG_CAPS_GENERIC;
    trace_dmsc_get_fw_caps(ti_dmsc_message_name_from_id(hdr->type),
                           ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FW CAPABILITIES response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_get_version(TIDmscClient *client, TISciMsgHdr *hdr,
                                uint16_t thread_id, const uint32_t *words,
                                size_t nwords)
{
    struct TiSciMsgVersionResp resp = {0};
    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.firmware_revision = 0x000a;
    resp.abi_major = 4;
    resp.abi_minor = 0;
    snprintf(resp.firmware_description, sizeof(resp.firmware_description),
             "QEMU_TI_DMSC (Wadims DMSC)");
    trace_dmsc_get_version(ti_dmsc_message_name_from_id(hdr->type),
                           ti_dmsc_host_name_from_id(hdr->host),
                           resp.firmware_description);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push VERSION response into sec-proxy "
                      "thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_device(TIDmscClient *client, TISciMsgHdr *hdr,
                                      uint16_t thread_id, const uint32_t *words,
                                      size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgGetDeviceReq *req = (struct TisciMsgGetDeviceReq *)words;
    struct TisciMsgGetDeviceResp resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    if (req->id < TISCI_DEV_ID_MAX) {
        resp.current_state = s->dev_hw_state[req->id];
        resp.programmed_state = s->dev_prog_state[req->id];
    } else {
        resp.current_state = resp.programmed_state =
            TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
    }

    trace_dmsc_handle_get_device(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host),
                                 ti_dmsc_device_name_from_id(req->id),
                                 resp.programmed_state, resp.current_state);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_DEVICE response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_status(TIDmscClient *client, TISciMsgHdr *hdr,
                                      uint16_t thread_id, const uint32_t *words,
                                      size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgProcGetStatusReq *req =
        (struct TisciMsgProcGetStatusReq *)words;
    struct TisciMsgProcGetStatusResp resp = {0};

    trace_dmsc_handle_get_status(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host),
                                 ti_dmsc_proc_name_from_id(req->processor_id),
                                 req->processor_id);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.processor_id = req->processor_id;
    resp.bootvector_lo = 0;
    resp.bootvector_hi = 0;
    resp.config_flags_1 = 0;
    resp.control_flags_1 = 0;
    resp.status_flags_1 = 0;

    if (req->processor_id == SCICLIENT_PROCID_A53_CL0_C0 ||
        req->processor_id == SCICLIENT_PROCID_A53_CL0_C1) {
        uint64_t bv =
            s->proc_bootvector[req->processor_id - SCICLIENT_PROCID_A53_CL0_C0];

        resp.bootvector_lo = (uint32_t)bv;
        resp.bootvector_hi = (uint32_t)(bv >> 32);
    }

    if (req->processor_id == SCICLIENT_PROCID_MCU_M4FSS0_C0) {
        resp.status_flags_1 |= TISCI_MSG_VAL_PROC_BOOT_STATUS_FLAG_M4F_WFI;
    }

    trace_dmsc_get_status_resp(ti_dmsc_proc_name_from_id(req->processor_id),
                               req->processor_id, resp.status_flags_1,
                               s->m4_running);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_STATUS response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * WAIT_PROC_BOOT_STATUS waits for processor WFE/WFI status bits. Per-core
 * polling is not modeled; no-response requests complete without a reply.
 */
static void ti_dmsc_handle_wait_proc_boot_status(TIDmscClient *client,
                                                 TISciMsgHdr *hdr,
                                                 uint16_t thread_id,
                                                 const uint32_t *words,
                                                 size_t nwords)
{
    struct TisciMsgReqWaitProcBootStatus *req =
        (struct TisciMsgReqWaitProcBootStatus *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_wait_proc_boot_status(
        ti_dmsc_message_name_from_id(hdr->type),
        ti_dmsc_host_name_from_id(hdr->host),
        ti_dmsc_proc_name_from_id(req->processor_id), req->processor_id);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push WAIT_PROC_BOOT_STATUS response "
                      "into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_device_state(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgSetDeviceReq *req = (struct TisciMsgSetDeviceReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_device_state(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       ti_dmsc_device_name_from_id(req->id),
                                       ti_dmsc_device_state_to_str(req->state));

    if (req->id < TISCI_DEV_ID_MAX) {
        s->dev_hw_state[req->id] = s->dev_prog_state[req->id] = req->state;
    }

    if (req->id == TISCI_DEV_MCU_M4FSS0_CORE0 &&
        req->state != TISCI_MSG_VALUE_DEVICE_SW_STATE_ON) {
        s->m4_running = false;
    }

    /*
     * SET_DEVICE ON releases an A53 core at the vector captured by
     * SET_CONFIG. Start the vCPU in EL3/AArch64; ALREADY_ON is harmless on
     * repeated handoffs.
     */
    if (req->id == TISCI_DEV_A53SS0_CORE_0 ||
        req->id == TISCI_DEV_A53SS0_CORE_1) {
        int core = req->id - TISCI_DEV_A53SS0_CORE_0;
        uint64_t cpuid = s->a53_cpu_id_base + core;

        if (req->state == TISCI_MSG_VALUE_DEVICE_SW_STATE_ON) {
            uint64_t entry = s->proc_bootvector[core];

            trace_dmsc_a53_start(core, entry);
            arm_set_cpu_on(cpuid, entry, 0, /* target_el */ 3,
                           /* target_aa64 */ true);
        } else if (req->state == TISCI_MSG_VALUE_DEVICE_SW_STATE_AUTO_OFF) {
            trace_dmsc_a53_stop(core);
            arm_set_cpu_off(cpuid);
        }
    }

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_DEVICE response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_device_resets(TIDmscClient *client,
                                             TISciMsgHdr *hdr,
                                             uint16_t thread_id,
                                             const uint32_t *words,
                                             size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgSetDeviceResetsReq *req =
        (struct TisciMsgSetDeviceResetsReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_device_resets(ti_dmsc_message_name_from_id(hdr->type),
                                        ti_dmsc_host_name_from_id(hdr->host),
                                        ti_dmsc_device_name_from_id(req->id),
                                        req->resets);

    if (req->id == TISCI_DEV_MCU_M4FSS0_CORE0) {
        s->m4_running = !(req->resets);
        if (req->resets == 1) {
            arm_set_cpu_off(s->m4_cpu_id);
        } else {
            arm_set_cpu_on_and_reset(s->m4_cpu_id);
        }
    }

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_DEVICE_RESETS response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * BOARD_CONFIG messages carry SYSFW board-configuration blobs. This model
 * accepts them, but does not consume or store the blobs.
 */
static void ti_dmsc_handle_board_config(TIDmscClient *client, TISciMsgHdr *hdr,
                                        uint16_t thread_id,
                                        const uint32_t *words, size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push BOARD_CONFIG response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * SET_CONFIG carries the A53 boot vector used later by SET_DEVICE. The
 * packed TISCI request puts bootvector_low at byte offset 9, so read it
 * with byte loads, not with a struct cast.
 */
static void ti_dmsc_handle_proc_set_config(TIDmscClient *client,
                                           TISciMsgHdr *hdr, uint16_t thread_id,
                                           const uint32_t *words, size_t nwords)
{
    TIDmscState *s = client->dmsc;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_proc_set_config(ti_dmsc_message_name_from_id(hdr->type),
                                      ti_dmsc_host_name_from_id(hdr->host));

    if (nwords * sizeof(uint32_t) >= sizeof(TISciMsgHdr) + 9) {
        /* Packed payload right after the 8-byte header. */
        const uint8_t *p = (const uint8_t *)words + sizeof(TISciMsgHdr);
        uint8_t proc_id = p[0];
        uint64_t bv = (uint64_t)(uint32_t)ldl_le_p(p + 1) |
                      ((uint64_t)(uint32_t)ldl_le_p(p + 5) << 32);

        if (proc_id == SCICLIENT_PROCID_A53_CL0_C0 ||
            proc_id == SCICLIENT_PROCID_A53_CL0_C1) {
            s->proc_bootvector[proc_id - SCICLIENT_PROCID_A53_CL0_C0] = bv;
            trace_dmsc_a53_bootvector(proc_id, bv);
        }
    }

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CONFIG response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * FWL_SET programs a firewall region. Firewall state is not modelled, so the
 * request is accepted and only a generic ACK/NAK is returned.
 */
static void ti_dmsc_handle_fwl_set(TIDmscClient *client, TISciMsgHdr *hdr,
                                   uint16_t thread_id, const uint32_t *words,
                                   size_t nwords)
{
    struct TisciMsgReqFwlSetFirewallRegion *req =
        (struct TisciMsgReqFwlSetFirewallRegion *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_fwl_set(ti_dmsc_message_name_from_id(hdr->type),
                              ti_dmsc_host_name_from_id(hdr->host), req->fwl_id,
                              req->region);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_SET response into sec-proxy "
                      "thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * FWL_GET returns one firewall region descriptor. The model echoes fwl_id
 * and region, and leaves control/permission fields zeroed.
 */
static void ti_dmsc_handle_fwl_get(TIDmscClient *client, TISciMsgHdr *hdr,
                                   uint16_t thread_id, const uint32_t *words,
                                   size_t nwords)
{
    struct TisciMsgReqFwlGetFirewallRegion *req =
        (struct TisciMsgReqFwlGetFirewallRegion *)words;
    struct TisciMsgRespFwlGetFirewallRegion resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fwl_id = req->fwl_id;
    resp.region = req->region;

    trace_dmsc_handle_fwl_get(ti_dmsc_message_name_from_id(hdr->type),
                              ti_dmsc_host_name_from_id(hdr->host), req->fwl_id,
                              req->region);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_GET response into sec-proxy "
                      "thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * FWL_CHANGE_OWNER changes the owner of a firewall region. Ownership is not
 * tracked; echo the IDs and owner index back.
 */
static void ti_dmsc_handle_fwl_change_owner(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    struct TisciMsgReqFwlChangeOwnerInfo *req =
        (struct TisciMsgReqFwlChangeOwnerInfo *)words;
    struct TisciMsgRespFwlChangeOwnerInfo resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fwl_id = req->fwl_id;
    resp.region = req->region;
    resp.owner_index = req->owner_index;

    trace_dmsc_handle_fwl_change_owner(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       req->fwl_id, req->region,
                                       req->owner_index);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_CHANGE_OWNER response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * SA2UL_GET_DKEK returns derived key data from SA2UL. No key ladder is
 * modelled; the response contains zero bytes and is no secret material.
 */
static void ti_dmsc_handle_sa2ul_get_dkek(TIDmscClient *client,
                                          TISciMsgHdr *hdr, uint16_t thread_id,
                                          const uint32_t *words, size_t nwords)
{
    struct TisciMsgReqSa2ulGetDkek *req =
        (struct TisciMsgReqSa2ulGetDkek *)words;
    struct TisciMsgRespSa2ulGetDkek resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_sa2ul_get_dkek(ti_dmsc_message_name_from_id(hdr->type),
                                     ti_dmsc_host_name_from_id(hdr->host),
                                     req->sa2ul_instance);

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SA2UL_GET_DKEK response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * READ_SWREV and READ_KEYCNT_KEYREV are security status queries. The status
 * words are not modelled and read back as zero.
 */
static void ti_dmsc_handle_read_swrev(TIDmscClient *client, TISciMsgHdr *hdr,
                                      uint16_t thread_id, const uint32_t *words,
                                      size_t nwords)
{
    struct TisciMsgRespReadSwrev resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_read_swrev(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push READ_SWREV response into "
                      "sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_read_keycnt_keyrev(TIDmscClient *client,
                                              TISciMsgHdr *hdr,
                                              uint16_t thread_id,
                                              const uint32_t *words,
                                              size_t nwords)
{
    struct TisciMsgRespReadKeycntKeyrev resp = {0};

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_read_keycnt_keyrev(
        ti_dmsc_message_name_from_id(hdr->type),
        ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, &resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push READ_KEYCNT_KEYREV response "
                      "into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    TIDmscState *s = TI_DMSC(dev);

    if (!s->sec_proxy) {
        error_setg(errp, "ti-dmsc: 'sec-proxy' link not set");
        return;
    }

    s->msg_words = ti_sec_proxy_get_msg_words(s->sec_proxy);
    if (s->msg_words == 0) {
        /* Fallback if sec-proxy does not provide it yet. */
        s->msg_words = TI_DMSC_MAX_WORDS;
    }

    if (s->msg_words > TI_DMSC_MAX_WORDS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: msg_words=%u too large, clamping to %u\n",
                      s->msg_words, TI_DMSC_MAX_WORDS);
        s->msg_words = TI_DMSC_MAX_WORDS;
    }

    if (s->num_rx_threads || s->num_tx_threads) {
        if (s->num_rx_threads != s->num_tx_threads) {
            error_setg(
                errp,
                "ti-dmsc: rx-threads and tx-threads must have the same length");
            return;
        }
        if (s->num_rx_threads == 0) {
            error_setg(errp, "ti-dmsc: rx-threads list is empty");
            return;
        }
        s->num_clients = s->num_rx_threads;
        s->clients = g_new0(TIDmscClient, s->num_clients);
        for (uint32_t i = 0; i < s->num_clients; i++) {
            s->clients[i].dmsc = s;
            s->clients[i].rx_thread_id = s->rx_thread_ids[i];
            s->clients[i].tx_thread_id = s->tx_thread_ids[i];
        }
    } else {
        s->num_clients = 1;
        s->clients = g_new0(TIDmscClient, s->num_clients);
        s->clients[0].dmsc = s;
        s->clients[0].rx_thread_id = s->rx_thread_id;
        s->clients[0].tx_thread_id = s->tx_thread_id;
    }

    for (uint32_t i = 0; i < s->num_clients; i++) {
        for (uint32_t j = 0; j < s->num_secure_rx_threads; j++) {
            if (s->clients[i].rx_thread_id == s->secure_rx_threads[j]) {
                s->clients[i].secure = true;
                break;
            }
        }
    }

    s->msg_handler[TISCI_MSG_PROC_RELEASE] = ti_dmsc_stop_proc;
    s->msg_handler[TISCI_MSG_PROC_REQUEST] = ti_dmsc_start_proc;
    s->msg_handler[TISCI_MSG_PROC_HANDOVER] = ti_dmsc_handover_proc;
    s->msg_handler[TISCI_MSG_SET_CTRL] = ti_dmsc_handle_proc_set_ctrl;
    s->msg_handler[TISCI_MSG_SYS_RESET] = ti_dmsc_handle_sys_reset;
    s->msg_handler[TISCI_MSG_QUERY_FW_CAPS] = ti_dmsc_query_hw_caps;
    s->msg_handler[TISCI_MSG_VERSION] = ti_dmsc_get_version;
    s->msg_handler[TISCI_MSG_GET_DEVICE] = ti_dmsc_handle_get_device;
    s->msg_handler[TISCI_MSG_SET_DEVICE] = ti_dmsc_handle_set_device_state;
    s->msg_handler[TISCI_MSG_GET_STATUS] = ti_dmsc_handle_get_status;
    s->msg_handler[TISCI_MSG_WAIT_PROC_BOOT_STATUS] =
        ti_dmsc_handle_wait_proc_boot_status;
    s->msg_handler[TISCI_MSG_SET_DEVICE_RESETS] =
        ti_dmsc_handle_set_device_resets;
    s->msg_handler[TISCI_MSG_GET_CLOCK] = ti_dmsc_handle_get_clock;
    s->msg_handler[TISCI_MSG_SET_CLOCK] = ti_dmsc_handle_set_clock;
    s->msg_handler[TISCI_MSG_GET_CLOCK_PARENT] =
        ti_dmsc_handle_get_clock_parent;
    s->msg_handler[TISCI_MSG_GET_NUM_CLOCK_PARENTS] =
        ti_dmsc_handle_get_clock_parents;
    s->msg_handler[TISCI_MSG_SET_CLOCK_PARENT] =
        ti_dmsc_handle_set_clock_parent;
    s->msg_handler[TISCI_MSG_QUERY_FREQ] = ti_dmsc_handle_query_freq;
    s->msg_handler[TISCI_MSG_GET_FREQ] = ti_dmsc_handle_get_freq;
    s->msg_handler[TISCI_MSG_SET_FREQ] = ti_dmsc_handle_set_freq;
    s->msg_handler[TISCI_MSG_SET_CONFIG] = ti_dmsc_handle_proc_set_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_RM] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_SECURITY] =
        ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_PM] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_FWL_SET] = ti_dmsc_handle_fwl_set;
    s->msg_handler[TISCI_MSG_FWL_GET] = ti_dmsc_handle_fwl_get;
    s->msg_handler[TISCI_MSG_FWL_CHANGE_OWNER] =
        ti_dmsc_handle_fwl_change_owner;
    s->msg_handler[TISCI_MSG_SA2UL_GET_DKEK] = ti_dmsc_handle_sa2ul_get_dkek;
    s->msg_handler[TISCI_MSG_READ_SWREV] = ti_dmsc_handle_read_swrev;
    s->msg_handler[TISCI_MSG_READ_KEYCNT_KEYREV] =
        ti_dmsc_handle_read_keycnt_keyrev;

    for (uint32_t i = 0; i < s->num_clients; i++) {
        ti_sec_proxy_register_msg_cb(s->sec_proxy, s->clients[i].rx_thread_id,
                                     ti_dmsc_sec_proxy_cb, &s->clients[i]);
    }

    ti_dmsc_init_device_states(s);

    /*
     * ti-dmsc is a pure QOM child with no bus/MMIO reset path. Register it
     * with the global reset container, so cold boot and system_reset both
     * requeue the DMSC boot notification.
     */
    qemu_register_resettable(OBJECT(dev));
}

static void ti_dmsc_init(Object *obj)
{
    TIDmscState *s = TI_DMSC(obj);

    qemu_mutex_init(&s->lock);
    s->bh = qemu_bh_new(ti_dmsc_bh, s);
    s->num_rx_threads = 0;
    s->rx_thread_ids = NULL;
    s->num_tx_threads = 0;
    s->tx_thread_ids = NULL;
    s->num_secure_rx_threads = 0;
    s->secure_rx_threads = NULL;
    s->num_clients = 0;
    s->clients = NULL;

    /*
     * Legacy single-client defaults; machines pass normally thread arrays.
     */
    /* Legacy M4 response thread default: 16. */

    s->msg_words = TI_DMSC_MAX_WORDS;
    object_property_add_link(
        obj, "sec-proxy", TYPE_TI_SEC_PROXY, (Object **)&s->sec_proxy,
        qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
}

static void ti_dmsc_finalize(Object *obj)
{
    TIDmscState *s = TI_DMSC(obj);

    if (s->bh) {
        qemu_bh_delete(s->bh);
        s->bh = NULL;
    }
    g_free(s->clients);
    s->clients = NULL;
    s->num_clients = 0;
    g_free(s->rx_thread_ids);
    s->rx_thread_ids = NULL;
    s->num_rx_threads = 0;
    g_free(s->tx_thread_ids);
    s->tx_thread_ids = NULL;
    s->num_tx_threads = 0;
    g_free(s->secure_rx_threads);
    s->secure_rx_threads = NULL;
    s->num_secure_rx_threads = 0;
    qemu_mutex_destroy(&s->lock);
}

static const Property ti_dmsc_props[] = {
    DEFINE_PROP_UINT16("rx-thread", TIDmscState, rx_thread_id, 17),
    DEFINE_PROP_UINT16("tx-thread", TIDmscState, tx_thread_id, 16),
    DEFINE_PROP_ARRAY("rx-threads", TIDmscState, num_rx_threads, rx_thread_ids,
                      qdev_prop_uint16, uint16_t),
    DEFINE_PROP_ARRAY("tx-threads", TIDmscState, num_tx_threads, tx_thread_ids,
                      qdev_prop_uint16, uint16_t),
    DEFINE_PROP_ARRAY("secure-rx-threads", TIDmscState, num_secure_rx_threads,
                      secure_rx_threads, qdev_prop_uint16, uint16_t),
    DEFINE_PROP_UINT64("m4-cpu-id", TIDmscState, m4_cpu_id, 0),
    DEFINE_PROP_UINT64("a53-cpu-id-base", TIDmscState, a53_cpu_id_base, 0),
};

static void ti_dmsc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = ti_dmsc_realize;
    rc->phases.hold = ti_dmsc_reset_hold;
    device_class_set_props(dc, ti_dmsc_props);
}

static const TypeInfo ti_dmsc_info = {
    .name = TYPE_TI_DMSC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(TIDmscState),
    .instance_init = ti_dmsc_init,
    .instance_finalize = ti_dmsc_finalize,
    .class_init = ti_dmsc_class_init,
};

static void ti_dmsc_types(void)
{
    type_register_static(&ti_dmsc_info);
}

type_init(ti_dmsc_types)
