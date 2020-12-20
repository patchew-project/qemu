/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * Goldfish TTY
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "qemu/log.h"
#include "trace.h"
#include "exec/address-spaces.h"
#include "hw/char/goldfish_tty.h"

/* registers */

enum {
    REG_PUT_CHAR      = 0x00,
    REG_BYTES_READY   = 0x04,
    REG_CMD           = 0x08,
    REG_DATA_PTR      = 0x10,
    REG_DATA_LEN      = 0x14,
    REG_DATA_PTR_HIGH = 0x18,
    REG_VERSION       = 0x20,
};

/* commands */

enum {
    CMD_INT_DISABLE   = 0x00,
    CMD_INT_ENABLE    = 0x01,
    CMD_WRITE_BUFFER  = 0x02,
    CMD_READ_BUFFER   = 0x03,
};

static uint64_t goldfish_tty_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    GoldfishTTYState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case REG_BYTES_READY:
        value = s->data_in_count;
        break;
    case REG_VERSION:
        value = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    trace_goldfish_tty_read(s, addr, size, value);

    return value;
}

static void goldfish_tty_cmd(GoldfishTTYState *s, uint32_t cmd)
{
    int to_copy;

    switch (cmd) {
    case CMD_INT_DISABLE:
        if (s->int_enabled) {
            if (s->data_in_count) {
                qemu_set_irq(s->irq, 0);
            }
            s->int_enabled = false;
        }
        break;
    case CMD_INT_ENABLE:
        if (!s->int_enabled) {
            if (s->data_in_count) {
                qemu_set_irq(s->irq, 1);
            }
            s->int_enabled = true;
        }
        break;
    case CMD_WRITE_BUFFER:
        to_copy = s->data_len;
        while (to_copy) {
            int len;

            len = MIN(GOLFISH_TTY_BUFFER_SIZE, to_copy);

            address_space_rw(&address_space_memory, s->data_ptr,
                             MEMTXATTRS_UNSPECIFIED, s->data_out, len, 0);
            to_copy -= len;
            qemu_chr_fe_write_all(&s->chr, s->data_out, len);
        }
        break;
    case CMD_READ_BUFFER:
        to_copy = MIN(s->data_len, s->data_in_count);
        address_space_rw(&address_space_memory, s->data_ptr,
                         MEMTXATTRS_UNSPECIFIED, s->data_in, to_copy, 1);
        s->data_in_count -= to_copy;
        memmove(s->data_in, s->data_in + to_copy, s->data_in_count);
        if (s->int_enabled && !s->data_in_count) {
            qemu_set_irq(s->irq, 0);
        }
        break;
    }
}

static void goldfish_tty_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    GoldfishTTYState *s = opaque;
    unsigned char c;

    trace_goldfish_tty_write(s, addr, size, value);

    switch (addr) {
    case REG_PUT_CHAR:
        c = value;
        qemu_chr_fe_write_all(&s->chr, &c, sizeof(c));
        break;
    case REG_CMD:
        goldfish_tty_cmd(s, value);
        break;
    case REG_DATA_PTR:
        s->data_ptr = value;
        break;
    case REG_DATA_PTR_HIGH:
        s->data_ptr = (value << 32) | (uint32_t)s->data_ptr;
        break;
    case REG_DATA_LEN:
        s->data_len = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps goldfish_tty_ops = {
    .read = goldfish_tty_read,
    .write = goldfish_tty_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
};

static int goldfish_tty_can_receive(void *opaque)
{
    GoldfishTTYState *s = opaque;
    int available = GOLFISH_TTY_BUFFER_SIZE - s->data_in_count;

    trace_goldfish_tty_can_receive(s, available);

    return available;
}

static void goldfish_tty_receive(void *opaque, const uint8_t *buffer, int size)
{
    GoldfishTTYState *s = opaque;

    trace_goldfish_tty_receive(s, size);

    g_assert(size <= GOLFISH_TTY_BUFFER_SIZE - s->data_in_count);

    memcpy(s->data_in + s->data_in_count, buffer, size);
    s->data_in_count += size;

    if (s->int_enabled && s->data_in_count) {
        qemu_set_irq(s->irq, 1);
    }
}

static void goldfish_tty_reset(DeviceState *dev)
{
    GoldfishTTYState *s = GOLDFISH_TTY(dev);

    trace_goldfish_tty_reset(s);

    memset(s->data_in, 0, GOLFISH_TTY_BUFFER_SIZE);
    memset(s->data_out, 0, GOLFISH_TTY_BUFFER_SIZE);
    s->data_in_count = 0;
    s->int_enabled = false;
    s->data_ptr = 0;
    s->data_len = 0;
}

static void goldfish_tty_realize(DeviceState *dev, Error **errp)
{
    GoldfishTTYState *s = GOLDFISH_TTY(dev);

    trace_goldfish_tty_realize(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &goldfish_tty_ops, s,
                          "goldfish_tty", 0x24);

    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr, goldfish_tty_can_receive,
                                 goldfish_tty_receive, NULL, NULL,
                                 s, NULL, true);
    }
}

static const VMStateDescription vmstate_goldfish_tty = {
    .name = "goldfish_tty",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data_len, GoldfishTTYState),
        VMSTATE_UINT64(data_ptr, GoldfishTTYState),
        VMSTATE_BOOL(int_enabled, GoldfishTTYState),
        VMSTATE_UINT32(data_in_count, GoldfishTTYState),
        VMSTATE_BUFFER(data_in, GoldfishTTYState),
        VMSTATE_BUFFER(data_out, GoldfishTTYState),
        VMSTATE_END_OF_LIST()
    }
};

static Property goldfish_tty_properties[] = {
    DEFINE_PROP_CHR("chardev", GoldfishTTYState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void goldfish_tty_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    GoldfishTTYState *s = GOLDFISH_TTY(obj);

    trace_goldfish_tty_instance_init(s);

    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);
}

static void goldfish_tty_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, goldfish_tty_properties);
    dc->reset = goldfish_tty_reset;
    dc->realize = goldfish_tty_realize;
    dc->vmsd = &vmstate_goldfish_tty;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo goldfish_tty_info = {
    .name = TYPE_GOLDFISH_TTY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = goldfish_tty_class_init,
    .instance_init = goldfish_tty_instance_init,
    .instance_size = sizeof(GoldfishTTYState),
};

static void goldfish_tty_register_types(void)
{
    type_register_static(&goldfish_tty_info);
}

type_init(goldfish_tty_register_types)
