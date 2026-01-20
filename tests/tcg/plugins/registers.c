/*
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "glib.h"
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/*
 * This callback is called when a vCPU is initialized. It tests whether we
 * successfully read from a register and write value back to it. It also tests
 * that read-only registers cannot be written to, i.e., we can read a read-only
 * register but writing to it fails.
 */
static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    g_autoptr(GArray) regs = qemu_plugin_get_registers();
    g_autoptr(GByteArray) buf = g_byte_array_sized_new(0);
    int sz = 0;

    /* Make sure we can read and write an arbitrary register */
    qemu_plugin_reg_descriptor *reg_desc = &g_array_index(regs,
            qemu_plugin_reg_descriptor, 0);
    g_assert(reg_desc->is_readonly == false);
    sz = qemu_plugin_read_register(reg_desc->handle, buf);
    g_assert(sz > 0);
    g_assert(sz == buf->len);
    sz = qemu_plugin_write_register(reg_desc->handle, buf);
    g_assert(sz > 0);
    g_assert(sz == buf->len);

    /*
     * Reset the buffer and find a read-only register. On each architecture, at
     * least the PC should be read-only because it's only supposed to be
     * modified via the qemu_plugin_set_pc() function.
     */
    g_byte_array_set_size(buf, 0);
    for (size_t i = 0; i < regs->len; i++) {
        reg_desc = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        if (reg_desc->is_readonly) {
            sz = qemu_plugin_read_register(reg_desc->handle, buf);
            g_assert(sz > 0);
            g_assert(sz == buf->len);
            break;
        } else {
            reg_desc = NULL;
        }
    }
    /* Make sure there is a read-only register and we cannot write to it */
    g_assert(reg_desc != NULL);
    sz = qemu_plugin_write_register(reg_desc->handle, buf);
    g_assert(sz == -1);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    return 0;
}
