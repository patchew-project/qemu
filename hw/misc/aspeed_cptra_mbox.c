/*
 * ASPEED Caliptra mailbox host interface (frontend) and peer base class.
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/aspeed_cptra_mbox.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "system/memory.h"
#include "trace.h"

static size_t cptra_mbox_padded_len(uint32_t dlen)
{
    return (size_t)((dlen + 3) / 4) * 4;
}

static void cptra_mbox_clear(AspeedCptraMboxState *s)
{
    s->locked = false;
    s->release_pending = false;
    s->user = 0;
    s->target_user = 0;
    s->target_user_valid = 0;
    s->cmd = 0;
    s->dlen = 0;
    s->execute = 0;
    s->target_status = 0;
    s->cmd_status = 0;
    s->hw_status = 0;
    memset(s->sram, 0, sizeof(s->sram));
}

static void cptra_mbox_submit(AspeedCptraMboxState *s)
{
    CptraMboxPeerClass *pc;
    g_autofree uint8_t *data = NULL;
    size_t len;

    if (s->dlen > CPTRA_MBOX0_SRAM_SIZE) {
        s->cmd_status = CPTRA_MBOX0_STATUS_CMD_FAILURE;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: DLEN 0x%x exceeds SRAM size\n", __func__, s->dlen);
        return;
    }

    if (!s->peer) {
        /* No Caliptra peer present: the command cannot be serviced. */
        s->cmd_status = CPTRA_MBOX0_STATUS_CMD_FAILURE;
        return;
    }

    len = cptra_mbox_padded_len(s->dlen);
    if (len) {
        data = g_malloc(len);
        for (size_t i = 0; i < len / 4; i++) {
            stl_le_p(data + i * 4, s->sram[i]);
        }
    }

    s->command_pending = true;
    s->release_pending = false;
    trace_cptra_mbox_execute(s->cmd, s->dlen);

    pc = CPTRA_MBOX_PEER_GET_CLASS(s->peer);
    pc->handle_execute(s->peer, s->cmd, s->dlen, data, len);
}

/* CptraMboxIf::complete - called by the peer when a command finishes. */
static void cptra_mbox_complete(CptraMboxIf *iface, uint32_t status,
                                uint32_t dlen, const uint8_t *data,
                                uint32_t len)
{
    AspeedCptraMboxState *s = ASPEED_CPTRA_MBOX(iface);

    s->command_pending = false;
    trace_cptra_mbox_complete(status, dlen);

    if (s->release_pending) {
        cptra_mbox_clear(s);
        return;
    }

    /*
     * Report the peer's status verbatim and write back whatever response
     * payload it returned. The peer signals a transport/command failure with
     * CPTRA_MBOX0_STATUS_CMD_FAILURE and no data.
     */
    s->cmd_status = status;
    s->dlen = dlen;
    if (data && len) {
        size_t words = MIN(len, CPTRA_MBOX0_SRAM_SIZE) / 4;

        for (size_t i = 0; i < words; i++) {
            s->sram[i] = ldl_le_p(data + i * 4);
        }
    }
}

static uint64_t cptra_mbox_sram_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedCptraMboxState *s = opaque;
    uint32_t idx = offset / 4;

    if (idx >= CPTRA_MBOX0_SRAM_WORDS) {
        return 0;
    }
    return s->sram[idx];
}

static void cptra_mbox_sram_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    AspeedCptraMboxState *s = opaque;
    uint32_t idx = offset / 4;

    if (idx < CPTRA_MBOX0_SRAM_WORDS) {
        s->sram[idx] = (uint32_t)value;
    }
}

static const MemoryRegionOps cptra_mbox_sram_ops = {
    .read = cptra_mbox_sram_read,
    .write = cptra_mbox_sram_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static uint64_t cptra_mbox_csr_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedCptraMboxState *s = opaque;

    switch (offset) {
    case CPTRA_MBOX0_LOCK_OFF:
        if (!s->locked) {
            s->locked = true;
            s->user = CPTRA_MBOX0_SOC_USER_ID;
            return 0;
        }
        return 1;
    case CPTRA_MBOX0_USER_OFF:
        return s->user;
    case CPTRA_MBOX0_TARGET_USER_OFF:
        return s->target_user;
    case CPTRA_MBOX0_TARGET_USER_VAL_OFF:
        return s->target_user_valid;
    case CPTRA_MBOX0_CMD_OFF:
        return s->cmd;
    case CPTRA_MBOX0_DLEN_OFF:
        return s->dlen;
    case CPTRA_MBOX0_EXECUTE_OFF:
        return s->execute;
    case CPTRA_MBOX0_TARGET_STATUS_OFF:
        return s->target_status;
    case CPTRA_MBOX0_CMD_STATUS_OFF:
        return s->cmd_status;
    case CPTRA_MBOX0_HW_STATUS_OFF:
        return s->hw_status;
    default:
        return 0;
    }
}

static void cptra_mbox_csr_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    AspeedCptraMboxState *s = opaque;
    uint32_t val = (uint32_t)value;

    switch (offset) {
    case CPTRA_MBOX0_LOCK_OFF:
    case CPTRA_MBOX0_USER_OFF:
    case CPTRA_MBOX0_HW_STATUS_OFF:
        break;
    case CPTRA_MBOX0_TARGET_USER_OFF:
        s->target_user = val;
        break;
    case CPTRA_MBOX0_TARGET_USER_VAL_OFF:
        s->target_user_valid = val;
        break;
    case CPTRA_MBOX0_CMD_OFF:
        s->cmd = val;
        break;
    case CPTRA_MBOX0_DLEN_OFF:
        if (val > CPTRA_MBOX0_SRAM_SIZE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: DLEN 0x%x exceeds SRAM size, clamped\n",
                          __func__, val);
            val = CPTRA_MBOX0_SRAM_SIZE;
        }
        s->dlen = val;
        break;
    case CPTRA_MBOX0_EXECUTE_OFF:
        if (val == 1 && s->execute != 1 && !s->command_pending) {
            s->execute = 1;
            s->cmd_status = CPTRA_MBOX0_STATUS_BUSY;
            cptra_mbox_submit(s);
        } else if (val == 0 && s->execute != 0) {
            if (s->command_pending) {
                s->release_pending = true;
            } else {
                cptra_mbox_clear(s);
            }
        }
        break;
    case CPTRA_MBOX0_TARGET_STATUS_OFF:
        s->target_status = val;
        break;
    case CPTRA_MBOX0_CMD_STATUS_OFF:
        s->cmd_status = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps cptra_mbox_csr_ops = {
    .read = cptra_mbox_csr_read,
    .write = cptra_mbox_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void cptra_mbox_reset_hold(Object *obj, ResetType type)
{
    AspeedCptraMboxState *s = ASPEED_CPTRA_MBOX(obj);

    if (s->command_pending) {
        /* Defer the clear until the in-flight command completes. */
        s->release_pending = true;
        return;
    }
    cptra_mbox_clear(s);

    if (s->peer) {
        CptraMboxPeerClass *pc = CPTRA_MBOX_PEER_GET_CLASS(s->peer);
        if (pc->handle_reset) {
            pc->handle_reset(s->peer);
        }
    }
}

static void cptra_mbox_peer_check(const Object *obj, const char *name,
                                  Object *val, Error **errp)
{
    CptraMboxPeer *peer;

    if (!val) {
        return;
    }

    peer = CPTRA_MBOX_PEER(val);
    if (peer->intf) {
        error_setg(errp, "Caliptra mailbox peer is already in use");
    }
}

bool aspeed_cptra_mbox_set_peer(AspeedCptraMboxState *s, CptraMboxPeer *peer,
                                Error **errp)
{
    CptraMboxPeer *old_peer = s->peer;

    if (old_peer == peer) {
        if (s->peer) {
            s->peer->intf = CPTRA_MBOX_IF(s);
        }
        return true;
    }
    if (!object_property_set_link(OBJECT(s), "peer",
                                  peer ? OBJECT(peer) : NULL, errp)) {
        return false;
    }
    if (old_peer) {
        old_peer->intf = NULL;
    }
    if (s->peer) {
        s->peer->intf = CPTRA_MBOX_IF(s);
    }
    return true;
}

static void cptra_mbox_init(Object *obj)
{
    AspeedCptraMboxState *s = ASPEED_CPTRA_MBOX(obj);

    object_property_add_link(obj, "peer", TYPE_CPTRA_MBOX_PEER,
                             (Object **)&s->peer, cptra_mbox_peer_check,
                             OBJ_PROP_LINK_STRONG);
}

static void cptra_mbox_realize(DeviceState *dev, Error **errp)
{
    AspeedCptraMboxState *s = ASPEED_CPTRA_MBOX(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->peer) {
        if (!aspeed_cptra_mbox_set_peer(s, s->peer, errp)) {
            return;
        }
    }

    memory_region_init_io(&s->sram_mr, OBJECT(s), &cptra_mbox_sram_ops, s,
                          "cptra-mbox.sram", CPTRA_MBOX0_SRAM_SIZE);
    sysbus_init_mmio(sbd, &s->sram_mr);

    memory_region_init_io(&s->csr_mr, OBJECT(s), &cptra_mbox_csr_ops, s,
                          "cptra-mbox.csr", CPTRA_MBOX0_CSR_SIZE);
    sysbus_init_mmio(sbd, &s->csr_mr);
}

static const VMStateDescription vmstate_cptra_mbox = {
    .name = TYPE_ASPEED_CPTRA_MBOX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(locked, AspeedCptraMboxState),
        VMSTATE_UINT32(user, AspeedCptraMboxState),
        VMSTATE_UINT32(target_user, AspeedCptraMboxState),
        VMSTATE_UINT32(target_user_valid, AspeedCptraMboxState),
        VMSTATE_UINT32(cmd, AspeedCptraMboxState),
        VMSTATE_UINT32(dlen, AspeedCptraMboxState),
        VMSTATE_UINT32(execute, AspeedCptraMboxState),
        VMSTATE_UINT32(target_status, AspeedCptraMboxState),
        VMSTATE_UINT32(cmd_status, AspeedCptraMboxState),
        VMSTATE_UINT32(hw_status, AspeedCptraMboxState),
        VMSTATE_UINT32_ARRAY(sram, AspeedCptraMboxState,
                             CPTRA_MBOX0_SRAM_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static void cptra_mbox_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    CptraMboxIfClass *ic = CPTRA_MBOX_IF_CLASS(oc);

    dc->desc = "Caliptra mailbox host interface";
    dc->realize = cptra_mbox_realize;
    dc->vmsd = &vmstate_cptra_mbox;
    rc->phases.hold = cptra_mbox_reset_hold;
    ic->complete = cptra_mbox_complete;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo cptra_mbox_types[] = {
    {
        .name          = TYPE_CPTRA_MBOX_IF,
        .parent        = TYPE_INTERFACE,
        .class_size    = sizeof(CptraMboxIfClass),
    },
    {
        .name          = TYPE_CPTRA_MBOX_PEER,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(CptraMboxPeer),
        .class_size    = sizeof(CptraMboxPeerClass),
        .abstract      = true,
    },
    {
        .name          = TYPE_ASPEED_CPTRA_MBOX,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedCptraMboxState),
        .instance_init = cptra_mbox_init,
        .class_init    = cptra_mbox_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { TYPE_CPTRA_MBOX_IF },
            { }
        },
    },
};

DEFINE_TYPES(cptra_mbox_types)
