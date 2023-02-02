/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * MIPS Trickbox
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "hw/misc/mips_trickbox.h"

static uint64_t mips_trickbox_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint64_t value = 0;

    qemu_log_mask(LOG_UNIMP,
                    "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                    __func__, addr);
    trace_mips_trickbox_read(size, value);

    return 0;
}

static void mips_trickbox_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    trace_mips_trickbox_write(size, val64);

    switch (addr) {
    case REG_SIM_CMD:
        switch (val64 & 0xffffffff) {
        case TRICK_PANIC:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_PANIC);
            break;
        case TRICK_HALT:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        case TRICK_SUSPEND:
            qemu_system_suspend_request();
            break;
        case TRICK_RESET:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            break;
        case TRICK_PASS_MIPS:
        case TRICK_PASS_NANOMIPS:
            exit(EXIT_SUCCESS);
            break;
        case TRICK_FAIL_MIPS:
        case TRICK_FAIL_NANOMIPS:
            exit(EXIT_FAILURE);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps mips_trickbox_ops = {
    .read = mips_trickbox_read,
    .write = mips_trickbox_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4
    }
};

static void mips_trickbox_init(Object *obj)
{
    MIPSTrickboxState *s = MIPS_TRICKBOX(obj);

    memory_region_init_io(&s->mmio, obj, &mips_trickbox_ops, s,
                          TYPE_MIPS_TRICKBOX, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo mips_trickbox_info = {
    .name          = TYPE_MIPS_TRICKBOX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSTrickboxState),
    .instance_init = mips_trickbox_init,
};

static void mips_trickbox_register_types(void)
{
    type_register_static(&mips_trickbox_info);
}

type_init(mips_trickbox_register_types)
