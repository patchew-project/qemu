/*
 * ASPEED Caliptra emulator backend
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "hw/arm/aspeed_caliptra_emu.h"
#include "qemu/error-report.h"

#ifdef CONFIG_ASPEED_CALIPTRA_CBINDING
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "system/runstate.h"

#include "caliptra_model.h"
#endif

#define CALIPTRA_EMU_PREFIX "[caliptra] "

#ifdef CONFIG_ASPEED_CALIPTRA_CBINDING
#define CALIPTRA_RUNTIME_READY_BOOT_STATUS 0x600

typedef struct AspeedCaliptraEmuState {
    QemuThread thread;
    VMChangeStateEntry *vmstate;
    struct caliptra_model *model;
    Chardev *console;
    char *rom_path;
    char *firmware_path;
    char *error;
    bool at_start_of_line;
    bool waiting_for_runtime;
    bool startup_pause_done;
} AspeedCaliptraEmuState;

static void caliptra_emu_write(AspeedCaliptraEmuState *s,
                               const uint8_t *buf, size_t len)
{
    g_autofree uint8_t *out = NULL;
    size_t prefix_len = strlen(CALIPTRA_EMU_PREFIX);
    size_t extra = 0;
    size_t i;
    size_t out_pos = 0;

    if (!s->console || len == 0) {
        return;
    }

    for (i = 0; i < len; i++) {
        if (s->at_start_of_line || (i > 0 && buf[i - 1] == '\n')) {
            extra += prefix_len;
        }
    }

    out = g_malloc(len + extra);
    for (i = 0; i < len; i++) {
        if (s->at_start_of_line) {
            memcpy(out + out_pos, CALIPTRA_EMU_PREFIX, prefix_len);
            out_pos += prefix_len;
            s->at_start_of_line = false;
        }

        out[out_pos++] = buf[i];
        if (buf[i] == '\n') {
            s->at_start_of_line = true;
        }
    }

    qemu_chr_write_all(s->console, out, out_pos);
}

static void caliptra_emu_note(AspeedCaliptraEmuState *s, const char *msg)
{
    if (!s->at_start_of_line) {
        static const uint8_t newline = '\n';
        qemu_chr_write_all(s->console, &newline, 1);
        s->at_start_of_line = true;
    }
    caliptra_emu_write(s, (const uint8_t *)msg, strlen(msg));
}

static bool caliptra_emu_read_file(const char *path, const char *name,
                                   gchar **contents, gsize *len, char **error)
{
    g_autoptr(GError) err = NULL;

    if (!g_file_get_contents(path, contents, len, &err)) {
        *error = g_strdup_printf("Failed to read Caliptra %s image '%s': %s",
                                 name, path, err->message);
        return false;
    }

    return true;
}

static void caliptra_emu_release_vm(AspeedCaliptraEmuState *s,
                                    const char *reason)
{
    if (!s->waiting_for_runtime) {
        return;
    }

    s->waiting_for_runtime = false;
    caliptra_emu_note(s, reason);
    if (s->startup_pause_done) {
        vm_start();
    }
}

static void caliptra_emu_vm_state_change(void *opaque, bool running,
                                         RunState state)
{
    AspeedCaliptraEmuState *s = opaque;

    if (!running || !s->waiting_for_runtime || s->startup_pause_done) {
        return;
    }

    s->startup_pause_done = true;
    /*
     * FIXME: RFC ONLY - Using vm_stop_force_state is a hack.
     * We need advice on the proper way to hold main CPUs in reset
     * until the Caliptra thread finishes booting.
     */
    vm_stop_force_state(RUN_STATE_PAUSED);
}

static void caliptra_emu_boot_done_bh(void *opaque)
{
    AspeedCaliptraEmuState *s = opaque;

    if (s->error) {
        error_report("%s", s->error);
        caliptra_emu_release_vm(s,
                                "runtime boot failed, releasing CA35\n");
    } else {
        caliptra_emu_release_vm(s, "runtime ready, releasing CA35\n");
    }

    qemu_del_vm_change_state_handler(s->vmstate);
    if (s->model) {
        caliptra_model_destroy(s->model);
    }
    g_free(s->rom_path);
    g_free(s->firmware_path);
    g_free(s->error);
    g_free(s);
}

static void *caliptra_emu_thread(void *opaque)
{
    AspeedCaliptraEmuState *s = opaque;
    g_autofree gchar *rom = NULL;
    g_autofree gchar *firmware = NULL;
    gsize rom_len = 0;
    gsize firmware_len = 0;
    static const uint8_t empty;
    struct caliptra_model_init_params params;
    int rc;

    if (!caliptra_emu_read_file(s->rom_path, "ROM", &rom, &rom_len,
                                &s->error) ||
        !caliptra_emu_read_file(s->firmware_path, "firmware", &firmware,
                                &firmware_len, &s->error)) {
        goto out;
    }

    params = (struct caliptra_model_init_params) {
        .rom = {
            .data = (const uint8_t *)rom,
            .len = rom_len,
        },
        .dccm = {
            .data = &empty,
            .len = 0,
        },
        .iccm = {
            .data = &empty,
            .len = 0,
        },
        .security_state = CALIPTRA_SEC_STATE_DBG_UNLOCKED_UNPROVISIONED,
    };

    rc = caliptra_model_init_default(params, &s->model);
    if (rc != CALIPTRA_MODEL_STATUS_OK) {
        s->error = g_strdup_printf(
            "Failed to initialize Caliptra c-binding model: %d", rc);
        goto out;
    }

    rc = caliptra_model_boot_default(s->model,
        (struct caliptra_buffer) {
            .data = (const uint8_t *)firmware,
            .len = firmware_len,
        },
        CALIPTRA_RUNTIME_READY_BOOT_STATUS);
    if (rc != CALIPTRA_MODEL_STATUS_OK) {
        s->error = g_strdup_printf("Failed to boot Caliptra runtime: %d", rc);
        goto out;
    }

out:
    aio_bh_schedule_oneshot(qemu_get_aio_context(), caliptra_emu_boot_done_bh,
                            s);
    return NULL;
}

void aspeed_caliptra_emu_start(Chardev *console, const char *rom_path,
                               const char *firmware_path)
{
    AspeedCaliptraEmuState *s;

    if (!rom_path && !firmware_path) {
        return;
    }

    if (!rom_path || !rom_path[0] || !firmware_path || !firmware_path[0]) {
        error_report("Both caliptra-rom and caliptra-firmware must be set");
        return;
    }

    if (!console) {
        error_report("Caliptra emulator backend is set "
                     "but serial0 is unavailable");
        return;
    }

    s = g_new0(AspeedCaliptraEmuState, 1);
    s->console = console;
    s->rom_path = g_strdup(rom_path);
    s->firmware_path = g_strdup(firmware_path);
    s->at_start_of_line = true;
    s->waiting_for_runtime = true;

    s->vmstate = qemu_add_vm_change_state_handler(caliptra_emu_vm_state_change,
                                                  s);
    caliptra_emu_note(s, "starting c-binding model thread\n");
    caliptra_emu_note(s, "waiting for runtime ready before starting CA35\n");

    qemu_thread_create(&s->thread, "caliptra-emu", caliptra_emu_thread, s,
                       QEMU_THREAD_DETACHED);
}
#else
void aspeed_caliptra_emu_start(Chardev *console, const char *rom_path,
                               const char *firmware_path)
{
    (void)console;

    if (!rom_path && !firmware_path) {
        return;
    }

    error_report("Caliptra c-binding backend was not compiled in");
}
#endif
