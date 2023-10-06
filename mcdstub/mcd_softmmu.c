/*
 * this handeles all system emulation functions for the mcdstub
 */

#include "exec/mcdstub.h"

int mcdserver_start(const char *device)
{
    trace_gdbstub_op_start(device);

    char gdbstub_device_name[128];
    Chardev *chr = NULL;
    Chardev *mon_chr;

    if (!first_cpu) {
        error_report("gdbstub: meaningless to attach gdb to a "
                     "machine without any CPU.");
        return -1;
    }

    if (!gdb_supports_guest_debug()) {
        error_report("gdbstub: current accelerator doesn't "
                     "support guest debugging");
        return -1;
    }

    if (!device) {
        return -1;
    }
    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(gdbstub_device_name, sizeof(gdbstub_device_name),
                     "%s,wait=off,nodelay=on,server=on", device);
            device = gdbstub_device_name;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = gdb_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
        }
#endif
        /*
         * FIXME: it's a bit weird to allow using a mux chardev here
         * and implicitly setup a monitor. We may want to break this.
         */
        chr = qemu_chr_new_noreplay("gdb", device, true, NULL);
        if (!chr) {
            return -1;
        }
    }

    if (!gdbserver_state.init) {
        gdb_init_gdbserver_state();

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_GDB,
                                   NULL, NULL, &error_abort);
        monitor_init_hmp(mon_chr, false, &error_abort);
    } else {
        qemu_chr_fe_deinit(&gdbserver_system_state.chr, true);
        mon_chr = gdbserver_system_state.mon_chr;
        reset_gdbserver_state();
    }

    create_processes(&gdbserver_state);

    if (chr) {
        qemu_chr_fe_init(&gdbserver_system_state.chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&gdbserver_system_state.chr,
                                 gdb_chr_can_receive,
                                 gdb_chr_receive, gdb_chr_event,
                                 NULL, &gdbserver_state, NULL, true);
    }
    gdbserver_state.state = chr ? RS_IDLE : RS_INACTIVE;
    gdbserver_system_state.mon_chr = mon_chr;
    gdb_syscall_reset();

    return 0;
}