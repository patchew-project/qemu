/*
 * QEMU System Emulator machine interface
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/rcu.h"
#include "qemu-common.h"
#include "chardev/char.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qapi-types-sockets.h"
#include "qemu/systemd.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "hw/qdev-core.h"

static void open_socket_and_monitor(void)
{
    int nfds = check_socket_activation();
    Chardev *chardev;
    if (nfds > 1) {
        error_report("QEMU only supports listening on one socket");
        exit(1);
    }
    if (!nfds) {
        ChardevBackend backend = {
            .type = CHARDEV_BACKEND_KIND_STDIO,
            .u.stdio.data = &(ChardevStdio) {
                .has_signal = true,
                .signal = false
            }
        };
        chardev = qemu_chardev_new("#qmp0", TYPE_CHARDEV_STDIO, &backend, NULL, &error_fatal);
    } else {
        ChardevBackend backend = {
           .type = CHARDEV_BACKEND_KIND_SOCKET,
           .u.socket.data = &(ChardevSocket) {
               .addr = &(SocketAddressLegacy) {
                   .type = SOCKET_ADDRESS_TYPE_FD,
                   .u.fd.data = &(String){
                       .str = (char *) stringify(FIRST_SOCKET_ACTIVATION_FD)
                   }
               }
           }
        };
        chardev = qemu_chardev_new("#qmp0", TYPE_CHARDEV_SOCKET, &backend, NULL, &error_fatal);
    }
    monitor_init_qmp(chardev, true, &error_fatal);
}

bool defaults_enabled(void)
{
    return false;
}

DisplayOptions *qmp_query_display_options(Error **errp)
{
    error_setg(errp, "You're running too fast!");
    return NULL;
}

Chardev *serial_hd(int i)
{
    return NULL;
}

void qmp_x_exit_preconfig(Error **errp)
{
    error_setg(errp, "You're running too fast!");
}

void qemu_init(int argc, char **argv, char **envp)
{
    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);

    qemu_init_subsystems();

    /* Missing: parse -name, -sandbox, -trace, -L */

    /*
     * Clear error location left behind by the loop.
     * Best done right after the loop.  Do not insert code here!
     */
    loc_set_none();

    /* Missing: process -name, -sandbox, -trace, -L */

    rcu_disable_atfork();
    qemu_init_main_loop(&error_fatal);
    cpu_timers_init();
    open_socket_and_monitor();

    /* Missing: replay_configure, configure_rtc */

    /* machine-set:
     *    qemu_create_machine();
     *    qemu_apply_machine_options();
     *    phase_advance(PHASE_MACHINE_CREATED);
     *
     * accel-set:
     *    configure_accelerators(argv[0]);
     *    phase_advance(PHASE_ACCEL_CREATED);
     *    machine type deprecation
     *    migration_object_init();
     *    cpu_type...
     *    accel_setup_post(current_machine);
     *    machine_run_board_init(current_machine, errp);
     */

    init_displaystate();
    os_setup_signal_handling();
}
