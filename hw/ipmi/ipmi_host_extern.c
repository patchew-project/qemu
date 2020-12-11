/*
 * IPMI Host external connection
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

/*
 * This is designed to connect to a host QEMU VM that runs ipmi_bmc_extern.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "hw/ipmi/ipmi.h"
#include "hw/ipmi/ipmi_host.h"
#include "hw/ipmi/ipmi_responder.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define VM_MSG_CHAR        0xA0 /* Marks end of message */
#define VM_CMD_CHAR        0xA1 /* Marks end of a command */
#define VM_ESCAPE_CHAR     0xAA /* Set bit 4 from the next byte to 0 */

#define VM_PROTOCOL_VERSION        1
#define VM_CMD_VERSION             0xff /* A version number byte follows */
#define VM_CMD_RESET               0x04
#define VM_CMD_CAPABILITIES        0x08

#define TYPE_IPMI_HOST_EXTERN "ipmi-host-extern"
#define IPMI_HOST_EXTERN(obj) OBJECT_CHECK(IPMIHostExtern, (obj), \
                                        TYPE_IPMI_HOST_EXTERN)

typedef struct IPMIHostExtern {
    IPMIHost parent;
    CharBackend chr;
    struct QEMUTimer *extern_timer;

    bool connected;
    uint8_t capability;

    unsigned char inbuf[MAX_IPMI_MSG_SIZE + 2];
    unsigned int inpos;
    bool in_escape;
    bool in_too_many;
    bool sending_cmd;

    unsigned char outbuf[(MAX_IPMI_MSG_SIZE + 2) * 2 + 1];
    unsigned int outpos;
    unsigned int outlen;
} IPMIHostExtern;

static unsigned char
ipmb_checksum(const unsigned char *data, int size, unsigned char start)
{
    unsigned char csum = start;

    for (; size > 0; size--, data++) {
            csum += *data;
    }
    return csum;
}

static void continue_send(IPMIHostExtern *ihe)
{
    int ret;

    if (ihe->outlen == 0) {
        return;
    }
    ret = qemu_chr_fe_write(&ihe->chr, ihe->outbuf + ihe->outpos,
                            ihe->outlen - ihe->outpos);
    if (ret > 0) {
        ihe->outpos += ret;
    }
    if (ihe->outpos < ihe->outlen) {
        /* Not fully transmitted, try again in a 10ms */
        timer_mod_ns(ihe->extern_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
    } else {
        /* Sent */
        ihe->outlen = 0;
        ihe->outpos = 0;
    }
}

static void extern_timeout(void *opaque)
{
    IPMIHostExtern *ihe = opaque;

    if (ihe->connected) {
        continue_send(ihe);
    }
}

static void addchar(IPMIHostExtern *ihe, unsigned char ch)
{
    switch (ch) {
    case VM_MSG_CHAR:
    case VM_CMD_CHAR:
    case VM_ESCAPE_CHAR:
        /* Escape the special characters. */
        ihe->outbuf[ihe->outlen++] = VM_ESCAPE_CHAR;
        ch |= 0x10;
        /* fall through */
    default:
        ihe->outbuf[ihe->outlen++] = ch;
    }
}

static void send_version(IPMIHostExtern *ihe)
{
    addchar(ihe, VM_CMD_VERSION);
    addchar(ihe, VM_PROTOCOL_VERSION);
    ihe->outbuf[ihe->outlen++] = VM_CMD_CHAR;
    continue_send(ihe);
}

/*
 * Handle a command (typically IPMI response) from IPMI responder
 * and send it out to the external host.
 */
static void ipmi_host_extern_handle_command(IPMIHost *h, uint8_t *cmd,
        unsigned cmd_len, unsigned max_cmd_len, uint8_t msg_id)
{
    IPMIHostExtern *ihe = IPMI_HOST_EXTERN(h);
    uint8_t err = 0, csum;
    int i;

    if (!ihe->connected) {
        /* We are not connected to external host. Just do nothing. */
        return;
    }
    addchar(ihe, msg_id);
    /* If it's too short or it was truncated, return an error. */
    if (cmd_len < 2) {
        err = IPMI_CC_REQUEST_DATA_LENGTH_INVALID;
    } else if ((cmd_len > max_cmd_len) || (cmd_len > MAX_IPMI_MSG_SIZE)) {
        err = IPMI_CC_REQUEST_DATA_TRUNCATED;
    }
    if (err) {
        /* Send out the error message */
        unsigned char rsp[3];

        rsp[0] = cmd[0] | 0x04;
        rsp[1] = cmd[1];
        rsp[2] = err;
        for (i = 0; i < 3; ++i) {
            addchar(ihe, rsp[i]);
        }
        csum = ipmb_checksum(&msg_id, 1, 0);
        addchar(ihe, -ipmb_checksum(rsp, 3, csum));
    } else {
        for (i = 0; i < cmd_len; i++) {
            addchar(ihe, cmd[i]);
        }
        csum = ipmb_checksum(&msg_id, 1, 0);
        addchar(ihe, -ipmb_checksum(cmd, cmd_len, csum));
    }

    ihe->outbuf[ihe->outlen++] = VM_MSG_CHAR;

    /* Start the transmit */
    continue_send(ihe);
}

/*
 * This function handles an IPMI message received from an external host by
 * sending it to the IPMI responder class.
 */
static void handle_msg(IPMIHostExtern *ihe)
{
    IPMIResponderClass *k = IPMI_RESPONDER_GET_CLASS(ihe->parent.responder);
    if (unlikely(ihe->in_escape)) {
        ipmi_debug("msg escape not ended\n");
        return;
    }
    if (unlikely(ihe->inpos < 4)) {
        ipmi_debug("msg too short\n");
        return;
    }
    if (unlikely(ihe->in_too_many)) {
        ihe->inbuf[3] = IPMI_CC_REQUEST_DATA_TRUNCATED;
        ihe->inpos = 4;
    } else if (unlikely(ipmb_checksum(ihe->inbuf, ihe->inpos, 0) != 0)) {
        ipmi_debug("msg checksum failure\n");
        return;
    } else {
        ihe->inpos--; /* Remove checkum */
    }

    k->handle_req(ihe->parent.responder, ihe->inbuf[0], ihe->inbuf + 1,
            ihe->inpos - 1);
}

/* This function handles a control command from the host. */
static void handle_command(IPMIHostExtern *ihe)
{
    uint8_t cmd;

    if (unlikely(ihe->in_too_many)) {
        ipmi_debug("cmd in too many\n");
        return;
    }

    if (unlikely(ihe->in_escape)) {
        ipmi_debug("cmd ends with escape character\n");
        return;
    }

    if (unlikely(ihe->inpos < 1)) {
        ipmi_debug("empty command.\n");
        return;
    }

    cmd = ihe->inbuf[0];
    switch (cmd) {
    case VM_CMD_VERSION:
        /* The host informs us the protocol version. */
        if (unlikely(ihe->inpos < 2)) {
            ipmi_debug("Host cmd version truncated.\n");
            break;
        }
        if (unlikely(ihe->inbuf[1] != VM_PROTOCOL_VERSION)) {
            ipmi_debug("Host protocol version %u is different from our version"
                    " %u\n", ihe->inbuf[1], VM_PROTOCOL_VERSION);
        }
        break;
    case VM_CMD_RESET:
        /* The host tells us a reset has happened. */
        break;
    case VM_CMD_CAPABILITIES:
        /* The host tells us its capability. */
        if (unlikely(ihe->inpos < 2)) {
            ipmi_debug("Host cmd capability truncated.\n");
            break;
        }
        ihe->capability = ihe->inbuf[1];
        break;
    default:
        /* The host shouldn't send us this command. Just ignore if they do. */
        ipmi_debug("Host cmd type %02x is invalid.\n", cmd);
    }
}

/* Clear the state of ipmi-host-extern. Happens at the end of a message. */
static void clear_state(IPMIHostExtern *ihe)
{
    ihe->in_escape = false;
    ihe->in_too_many = false;
    ihe->inpos = 0;
}

/* We always welcome an incoming request. */
static int can_receive(void *opaque)
{
    return 1;
}

/*
 * This function mirrors ipmi-bmc-extern. It handles an incoming character
 * sequence and translates it into IPMI message.
 */
static void receive(void *opaque, const uint8_t *buf, int size)
{
    IPMIHostExtern *ihe = opaque;
    int i;

    for (i = 0; i < size; ++i) {
        uint8_t ch = buf[i];

        switch (ch) {
        case VM_MSG_CHAR:
            /* The preceding characters are an IPMI message. */
            handle_msg(ihe);
            clear_state(ihe);
            break;

        case VM_CMD_CHAR:
            /* The preceding characters are a control command. */
            handle_command(ihe);
            clear_state(ihe);
            break;

        case VM_ESCAPE_CHAR:
            ihe->in_escape = true;
            break;

        default:
            if (ihe->in_escape) {
                ch &= ~0x10;
                ihe->in_escape = false;
            }
            if (ihe->in_too_many) {
                break;
            }
            if (ihe->inpos >= ARRAY_SIZE(ihe->inbuf)) {
                ihe->in_too_many = true;
                break;
            }
            ihe->inbuf[ihe->inpos++] = ch;
            break;
        }
    }
    return;
}

static void chr_event(void *opaque, QEMUChrEvent event)
{
    IPMIHostExtern *ihe = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        ihe->connected = true;
        clear_state(ihe);
        send_version(ihe);
        break;

    case CHR_EVENT_CLOSED:
        ihe->connected = false;
        break;

    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;

    default:
        g_assert_not_reached();
    }
}

static void ipmi_host_extern_responder_check(const Object *obj,
        const char *name, Object *val, Error **errp)
{
    IPMIHostExtern *ihe = IPMI_HOST_EXTERN(obj);

    if (ihe->parent.responder) {
        error_setg(errp, "IPMI host already has a responder");
    }
}

static void ipmi_host_extern_realize(DeviceState *dev, Error **errp)
{
    IPMIHostExtern *ihe = IPMI_HOST_EXTERN(dev);
    IPMIResponderClass *rk;

    if (!qemu_chr_fe_backend_connected(&ihe->chr)) {
        error_setg(errp, "IPMI external host requires chardev attribute");
        return;
    }

    qemu_chr_fe_set_handlers(&ihe->chr, can_receive, receive,
                             chr_event, NULL, ihe, NULL, true);

    if (ihe->parent.responder == NULL) {
        error_setg(errp, "IPMI host requires responder attribute");
        return;
    }
    rk = IPMI_RESPONDER_GET_CLASS(ihe->parent.responder);
    rk->set_host(ihe->parent.responder, &ihe->parent);
}

static const VMStateDescription vmstate_ipmi_host_extern = {
    .name = TYPE_IPMI_HOST_EXTERN,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_host_extern_init(Object *obj)
{
    IPMIHostExtern *ihe = IPMI_HOST_EXTERN(obj);

    object_property_add_link(OBJECT(ihe), "responder", TYPE_IPMI_RESPONDER,
            (Object **)(&ihe->parent.responder),
            ipmi_host_extern_responder_check, OBJ_PROP_LINK_STRONG);
    ihe->extern_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, extern_timeout, ihe);
    vmstate_register(NULL, 0, &vmstate_ipmi_host_extern, ihe);
}

static void ipmi_host_extern_finalize(Object *obj)
{
    IPMIHostExtern *ihe = IPMI_HOST_EXTERN(obj);

    timer_del(ihe->extern_timer);
    timer_free(ihe->extern_timer);
}

static Property ipmi_host_extern_properties[] = {
    DEFINE_PROP_CHR("chardev", IPMIHostExtern, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void ipmi_host_extern_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIHostClass *bk = IPMI_HOST_CLASS(oc);

    bk->handle_command = ipmi_host_extern_handle_command;
    dc->hotpluggable = false;
    dc->realize = ipmi_host_extern_realize;
    device_class_set_props(dc, ipmi_host_extern_properties);
}

static const TypeInfo ipmi_host_extern_type = {
    .name          = TYPE_IPMI_HOST_EXTERN,
    .parent        = TYPE_IPMI_HOST,
    .instance_size = sizeof(IPMIHostExtern),
    .instance_init = ipmi_host_extern_init,
    .instance_finalize = ipmi_host_extern_finalize,
    .class_init    = ipmi_host_extern_class_init,
 };

static void ipmi_host_extern_register_types(void)
{
    type_register_static(&ipmi_host_extern_type);
}

type_init(ipmi_host_extern_register_types)
