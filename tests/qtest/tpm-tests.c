/*
 * QTest TPM common test code
 *
 * Copyright (c) 2018 IBM Corporation
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/registerfields.h"
#include "hw/acpi/tpm.h"
#include "libqtest-single.h"
#include "tpm-emu.h"
#include "tpm-tests.h"

static bool
tpm_test_swtpm_skip(void)
{
    if (!tpm_util_swtpm_has_tpm2()) {
        g_test_skip("swtpm not in PATH or missing --tpm2 support");
        return true;
    }

    return false;
}

void tpm_test_swtpm_test(const char *src_tpm_path, tx_func *tx,
                         const char *ifmodel, const char *machine_options)
{
    char *args = NULL;
    QTestState *s;
    SocketAddress *addr = NULL;
    gboolean succ;
    GPid swtpm_pid;
    GError *error = NULL;

    if (tpm_test_swtpm_skip()) {
        return;
    }

    succ = tpm_util_swtpm_start(src_tpm_path, &swtpm_pid, &addr, &error);
    g_assert_true(succ);

    args = g_strdup_printf(
        "%s "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device %s,tpmdev=dev",
        machine_options ? : "", addr->u.q_unix.path, ifmodel);

    s = qtest_start(args);
    g_free(args);

    tpm_util_startup(s, tx);
    tpm_util_pcrextend(s, tx);

    static const unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(s, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_end();
    tpm_util_swtpm_kill(swtpm_pid);

    g_unlink(addr->u.q_unix.path);
    qapi_free_SocketAddress(addr);
}

void tpm_test_swtpm_migration_test(const char *src_tpm_path,
                                   const char *dst_tpm_path,
                                   const char *uri, tx_func *tx,
                                   const char *ifmodel,
                                   const char *machine_options)
{
    gboolean succ;
    GPid src_tpm_pid, dst_tpm_pid;
    SocketAddress *src_tpm_addr = NULL, *dst_tpm_addr = NULL;
    GError *error = NULL;
    QTestState *src_qemu, *dst_qemu;

    if (tpm_test_swtpm_skip()) {
        return;
    }

    succ = tpm_util_swtpm_start(src_tpm_path, &src_tpm_pid,
                                &src_tpm_addr, &error);
    g_assert_true(succ);

    succ = tpm_util_swtpm_start(dst_tpm_path, &dst_tpm_pid,
                                &dst_tpm_addr, &error);
    g_assert_true(succ);

    tpm_util_migration_start_qemu(&src_qemu, &dst_qemu,
                                  src_tpm_addr, dst_tpm_addr, uri,
                                  ifmodel, machine_options);

    tpm_util_startup(src_qemu, tx);
    tpm_util_pcrextend(src_qemu, tx);

    static const unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(src_qemu, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    tpm_util_migrate(src_qemu, uri);
    tpm_util_wait_for_migration_complete(src_qemu);

    tpm_util_pcrread(dst_qemu, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_quit(dst_qemu);
    qtest_quit(src_qemu);

    tpm_util_swtpm_kill(dst_tpm_pid);
    g_unlink(dst_tpm_addr->u.q_unix.path);
    qapi_free_SocketAddress(dst_tpm_addr);

    tpm_util_swtpm_kill(src_tpm_pid);
    g_unlink(src_tpm_addr->u.q_unix.path);
    qapi_free_SocketAddress(src_tpm_addr);
}

#define TPM_CMD "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00"

void tpm_test_crb(const void *data)
{
    const TPMTestState *s = data;
    uint32_t intfid = readl(tpm_device_base_addr + A_CRB_INTF_ID);
    uint32_t csize = readl(tpm_device_base_addr + A_CRB_CTRL_CMD_SIZE);
    uint64_t caddr = readq(tpm_device_base_addr + A_CRB_CTRL_CMD_LADDR);
    uint32_t rsize = readl(tpm_device_base_addr + A_CRB_CTRL_RSP_SIZE);
    uint64_t raddr = readq(tpm_device_base_addr + A_CRB_CTRL_RSP_LADDR);
    uint8_t locstate = readb(tpm_device_base_addr + A_CRB_LOC_STATE);
    uint32_t locctrl = readl(tpm_device_base_addr + A_CRB_LOC_CTRL);
    uint32_t locsts = readl(tpm_device_base_addr + A_CRB_LOC_STS);
    uint32_t sts = readl(tpm_device_base_addr + A_CRB_CTRL_STS);

    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceType), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceVersion), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRBIdleBypass), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapDataXferSizeSupport),
                    ==, 3);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapFIFO), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRB), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceSelector), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, RID), ==, 0);

    g_assert_cmpint(csize, >=, 128);
    g_assert_cmpint(rsize, >=, 128);
    g_assert_cmpint(caddr, >, tpm_device_base_addr);
    g_assert_cmpint(raddr, >, tpm_device_base_addr);

    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

    g_assert_cmpint(locctrl, ==, 0);

    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, Granted), ==, 0);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, beenSeized), ==, 0);

    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 1);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    /* request access to locality 0 */
    writeb(tpm_device_base_addr + A_CRB_LOC_CTRL, 1);

    /* granted bit must be set now */
    locsts = readl(tpm_device_base_addr + A_CRB_LOC_STS);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, Granted), ==, 1);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, beenSeized), ==, 0);

    /* we must have an assigned locality */
    locstate = readb(tpm_device_base_addr + A_CRB_LOC_STATE);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

    /* set into ready state */
    writel(tpm_device_base_addr + A_CRB_CTRL_REQ, 1);

    /* TPM must not be in the idle state */
    sts = readl(tpm_device_base_addr + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    memwrite(caddr, TPM_CMD, sizeof(TPM_CMD));

    uint32_t start = 1;
    uint64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    writel(tpm_device_base_addr + A_CRB_CTRL_START, start);
    do {
        start = readl(tpm_device_base_addr + A_CRB_CTRL_START);
        if ((start & 1) == 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);
    start = readl(tpm_device_base_addr + A_CRB_CTRL_START);
    g_assert_cmpint(start & 1, ==, 0);

    /* TPM must still not be in the idle state */
    sts = readl(tpm_device_base_addr + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    struct tpm_hdr tpm_msg;
    memread(raddr, &tpm_msg, sizeof(tpm_msg));
    g_assert_cmpmem(&tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* set TPM into idle state */
    writel(tpm_device_base_addr + A_CRB_CTRL_REQ, 2);

    /* idle state must be indicated now */
    sts = readl(tpm_device_base_addr + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 1);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    /* relinquish locality */
    writel(tpm_device_base_addr + A_CRB_LOC_CTRL, 2);

    /* Granted flag must be cleared */
    sts = readl(tpm_device_base_addr + A_CRB_LOC_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_LOC_STS, Granted), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_LOC_STS, beenSeized), ==, 0);

    /* no locality may be assigned */
    locstate = readb(tpm_device_base_addr + A_CRB_LOC_STATE);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

}
