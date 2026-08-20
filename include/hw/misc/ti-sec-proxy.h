/*
 * TI K3 secure proxy
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TI_SEC_PROXY_H
#define TI_SEC_PROXY_H

#include "hw/core/sysbus.h"
#include "hw/core/register.h"
#include "system/dma.h"
#include "qom/object.h"

#define TYPE_TI_SEC_PROXY "ti.sec-proxy"

#define RMAX_TI_SEC_PROXY (3)

OBJECT_DECLARE_SIMPLE_TYPE(TISecProxyState, TI_SEC_PROXY)

typedef void (*TISecProxyMsgCb)(void *opaque,
                               uint16_t thread_id,
                               const uint32_t *words,
                               size_t nwords);

enum TISciThreadIds {
    MAIN_0_R5_0_READ_RESPONSE_THREAD_ID = 0,
    MAIN_0_R5_0_WRITE_THREAD_ID = 1,
    MAIN_0_R5_1_READ_RESPONSE_THREAD_ID = 2,
    MAIN_0_R5_1_WRITE_THREAD_ID = 3,
    MAIN_0_R5_2_READ_RESPONSE_THREAD_ID = 4,
    MAIN_0_R5_2_WRITE_THREAD_ID = 5,
    MAIN_0_R5_3_READ_RESPONSE_THREAD_ID = 6,
    MAIN_0_R5_3_WRITE_THREAD_ID = 7,
    A53_0_READ_RESPONSE_THREAD_ID = 8,
    A53_0_WRITE_THREAD_ID = 9,
    A53_1_READ_RESPONSE_THREAD_ID = 10,
    A53_1_WRITE_THREAD_ID = 11,
    A53_2_READ_RESPONSE_THREAD_ID = 12,
    A53_2_WRITE_THREAD_ID = 13,
    A53_3_READ_RESPONSE_THREAD_ID = 14,
    A53_3_WRITE_THREAD_ID = 15,
    M4_0_READ_RESPONSE_THREAD_ID = 16,
    M4_0_WRITE_THREAD_ID = 17,
    MAIN_1_R5_0_READ_RESPONSE_THREAD_ID = 18,
    MAIN_1_R5_0_WRITE_THREAD_ID = 19,
    MAIN_1_R5_1_READ_RESPONSE_THREAD_ID = 20,
    MAIN_1_R5_1_WRITE_THREAD_ID = 21,
    MAIN_1_R5_2_READ_RESPONSE_THREAD_ID = 22,
    MAIN_1_R5_2_WRITE_THREAD_ID = 23,
    MAIN_1_R5_3_READ_RESPONSE_THREAD_ID = 24,
    MAIN_1_R5_3_WRITE_THREAD_ID = 25,
    A53_4_READ_RESPONSE_THREAD_ID = 26,
    A53_4_WRITE_THREAD_ID = 27,
    ICSSG_0_READ_RESPONSE_THREAD_ID = 28,
    ICSSG_0_WRITE_THREAD_ID = 29,
    ICSSG_1_READ_RESPONSE_THREAD_ID = 30,
    ICSSG_1_WRITE_THREAD_ID = 31,
    SEC_PROXY_THREAD_ID_MAX,
};

#define SEC_PROXY_MSG_MAX_WORDS (16)
#define SEC_PROXY_MSG_FIFO_DEPTH (16)

struct TISecProxyThreadInfo {
    uint8_t thread_id;
    uint8_t num_messages;
    uint32_t current_message[SEC_PROXY_MSG_MAX_WORDS];
    bool is_outbound;
    /* Callback invoked on commit (outbound only) */
    TISecProxyMsgCb cb;
    void *cb_opaque;
};

struct TISecProxyState {
    SysBusDevice parent_obj;
    MemoryRegion iomem_scfg;
    MemoryRegion iomem_rt;
    MemoryRegion iomem_target_data;
    RegisterInfoArray *reg_array;
    qemu_irq irq_evt;
    uint32_t regs[RMAX_TI_SEC_PROXY];
    RegisterInfo regs_info[RMAX_TI_SEC_PROXY];
    struct TISecProxyThreadInfo thread_info[SEC_PROXY_THREAD_ID_MAX];
    uint32_t msg_words;
};


/* Backend API used by ti-dmsc, resp. other consumers */
void ti_sec_proxy_register_msg_cb(TISecProxyState *sp,
                                  uint16_t thread_id,
                                  TISecProxyMsgCb cb,
                                  void *opaque);

size_t ti_sec_proxy_push_msg(TISecProxyState *sp, uint16_t thread_id,
                             const uint32_t *words, size_t nbytes);

uint32_t ti_sec_proxy_get_msg_words(TISecProxyState *sp);

/*
 * Clear an outbound thread counter before requeueing unsolicited messages.
 * push_msg() only increments it; reads clear only inbound threads.
 */
void ti_sec_proxy_reset_thread_count(TISecProxyState *sp, uint16_t thread_id);
#endif
