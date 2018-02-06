/*
 * QEMU KVM Hyper-V support
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TARGET_I386_HYPERV_H
#define TARGET_I386_HYPERV_H

#include "cpu.h"
#include "sysemu/kvm.h"
#include "qemu/event_notifier.h"

typedef struct HvSintRoute HvSintRoute;
typedef void (*HvSintMsgCb)(void *data, int status);

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit);

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data);
void hyperv_sint_route_ref(HvSintRoute *sint_route);
void hyperv_sint_route_unref(HvSintRoute *sint_route);

int kvm_hv_sint_route_set_sint(HvSintRoute *sint_route);

uint32_t hyperv_vp_index(X86CPU *cpu);
X86CPU *hyperv_find_vcpu(uint32_t vp_index);

int hyperv_synic_add(X86CPU *cpu);
void hyperv_synic_reset(X86CPU *cpu);
void hyperv_synic_update(X86CPU *cpu);

bool hyperv_synic_usable(void);

int hyperv_post_msg(HvSintRoute *sint_route, struct hyperv_message *msg);

int hyperv_set_evt_flag(HvSintRoute *sint_route, unsigned evtno);

struct hyperv_post_message_input;
typedef uint64_t (*HvMsgHandler)(const struct hyperv_post_message_input *msg,
                                 void *data);
int hyperv_set_msg_handler(uint32_t conn_id, HvMsgHandler handler, void *data);

int hyperv_set_evt_notifier(uint32_t conn_id, EventNotifier *notifier);

#endif
