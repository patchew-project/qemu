#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/i386/remote.h"
#include "io/channel.h"
#include "io/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"

gboolean mpqemu_process_msg(QIOChannel *ioc, GIOCondition cond,
                            gpointer opaque)
{
    Error *local_err = NULL;
    MPQemuMsg msg = { 0 };

    if (cond & G_IO_HUP) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }

    if (cond & (G_IO_ERR | G_IO_NVAL)) {
        error_setg(&local_err, "Error %d while processing message from proxy \
                   in remote process pid=%d", errno, getpid());
        return FALSE;
    }

    if (mpqemu_msg_recv(&msg, ioc) < 0) {
        return FALSE;
    }

    if (!mpqemu_msg_valid(&msg)) {
        error_report("Received invalid message from proxy \
                     in remote process pid=%d", getpid());
        return TRUE;
    }

    switch (msg.cmd) {
    default:
        error_setg(&local_err, "Unknown command (%d) received from proxy \
                   in remote process pid=%d", msg.cmd, getpid());
    }

    if (msg.data2) {
        free(msg.data2);
    }

    if (local_err) {
        error_report_err(local_err);
        return FALSE;
    }

    return TRUE;
}
