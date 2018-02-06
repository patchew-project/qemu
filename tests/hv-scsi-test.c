/*
 * QTest testcase for Hyper-V/VMBus SCSI
 *
 * Copyright (c) 2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <unistd.h>
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "libqos/libqos-pc.h"

static QOSState *qhv_scsi_start(const char *extra_opts)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-machine accel=kvm,vmbus "
                      "-cpu kvm64,hv_synic,hv_vpindex "
                      "-drive id=hd0,if=none,file=null-co://,format=raw "
                      "-device hv-scsi,id=scsi0 "
                      "-device scsi-hd,bus=scsi0.0,drive=hd0 %s";

    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_printerr("Hyper-V / VMBus are only available on x86\n");
        exit(EXIT_FAILURE);
    }

    if (access("/dev/kvm", R_OK | W_OK)) {
        g_printerr("Hyper-V / VMBus can only be used with KVM\n");
        exit(EXIT_FAILURE);
    }

    return qtest_pc_boot(cmd, extra_opts ? : "");
}

static void qhv_scsi_stop(QOSState *qs)
{
    qtest_shutdown(qs);
}

static void start_stop(void)
{
    QOSState *qs;

    qs = qhv_scsi_start(NULL);
    qhv_scsi_stop(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hv-scsi/start-stop", start_stop);

    return g_test_run();
}
