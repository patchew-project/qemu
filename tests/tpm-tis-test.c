/*
 * QTest testcase for TPM CRB
 *
 * Copyright (c) 2018 Red Hat, Inc.
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "hw/tpm/tpm_ioctl.h"
#include "io/channel-socket.h"
#include "libqtest.h"
#include "qapi/error.h"

#define TPM_RC_FAILURE 0x101
#define TPM2_ST_NO_SESSIONS 0x8001

#define TIS_REG(LOCTY, REG) \
    (TPM_TIS_ADDR_BASE + ((LOCTY) << 12) + REG)

struct tpm_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t code; /*ordinal/error */
    char buffer[];
} QEMU_PACKED;

#define DEBUG_TIS_TEST 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS_TEST) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define DPRINTF_ACCESS \
    DPRINTF("%s: %d: locty=%d l=%d access=0x%02x pending_request_flag=0x%x\n", \
            __func__, __LINE__, locty, l, access, pending_request_flag)

#define DPRINTF_STS \
    DPRINTF("%s: %d: sts = 0x%08x\n", __func__, __LINE__, sts)

typedef struct TestState {
    CompatGMutex data_mutex;
    CompatGCond data_cond;
    SocketAddress *addr;
    QIOChannel *tpm_ioc;
    GThread *emu_tpm_thread;
    struct tpm_hdr *tpm_msg;
} TestState;

static void test_wait_cond(TestState *s)
{
    gint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    g_mutex_lock(&s->data_mutex);
    if (!g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
        g_assert_not_reached();
    }
    g_mutex_unlock(&s->data_mutex);
}

static void *emu_tpm_thread(void *data)
{
    TestState *s = data;
    QIOChannel *ioc = s->tpm_ioc;

    s->tpm_msg = g_new(struct tpm_hdr, 1);
    while (true) {
        int minhlen = sizeof(s->tpm_msg->tag) + sizeof(s->tpm_msg->len);

        if (!qio_channel_read(ioc, (char *)s->tpm_msg, minhlen, &error_abort)) {
            break;
        }
        s->tpm_msg->tag = be16_to_cpu(s->tpm_msg->tag);
        s->tpm_msg->len = be32_to_cpu(s->tpm_msg->len);
        g_assert_cmpint(s->tpm_msg->len, >=, minhlen);
        g_assert_cmpint(s->tpm_msg->tag, ==, TPM2_ST_NO_SESSIONS);

        s->tpm_msg = g_realloc(s->tpm_msg, s->tpm_msg->len);
        qio_channel_read(ioc, (char *)&s->tpm_msg->code,
                         s->tpm_msg->len - minhlen, &error_abort);
        s->tpm_msg->code = be32_to_cpu(s->tpm_msg->code);

        /* reply error */
        s->tpm_msg->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
        s->tpm_msg->len = cpu_to_be32(sizeof(struct tpm_hdr));
        s->tpm_msg->code = cpu_to_be32(TPM_RC_FAILURE);
        qio_channel_write(ioc, (char *)s->tpm_msg, be32_to_cpu(s->tpm_msg->len),
                          &error_abort);
    }

    g_free(s->tpm_msg);
    s->tpm_msg = NULL;
    object_unref(OBJECT(s->tpm_ioc));
    return NULL;
}

static void *emu_ctrl_thread(void *data)
{
    TestState *s = data;
    QIOChannelSocket *lioc = qio_channel_socket_new();
    QIOChannel *ioc;

    qio_channel_socket_listen_sync(lioc, s->addr, &error_abort);
    g_cond_signal(&s->data_cond);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    ioc = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(ioc);

    {
        uint32_t cmd = 0;
        struct iovec iov = { .iov_base = &cmd, .iov_len = sizeof(cmd) };
        int *pfd = NULL;
        size_t nfd = 0;

        qio_channel_readv_full(ioc, &iov, 1, &pfd, &nfd, &error_abort);
        cmd = be32_to_cpu(cmd);
        g_assert_cmpint(cmd, ==, CMD_SET_DATAFD);
        g_assert_cmpint(nfd, ==, 1);
        s->tpm_ioc = QIO_CHANNEL(qio_channel_socket_new_fd(*pfd, &error_abort));
        g_free(pfd);

        cmd = 0;
        qio_channel_write(ioc, (char *)&cmd, sizeof(cmd), &error_abort);

        s->emu_tpm_thread = g_thread_new(NULL, emu_tpm_thread, s);
    }

    while (true) {
        uint32_t cmd;
        ssize_t ret;

        ret = qio_channel_read(ioc, (char *)&cmd, sizeof(cmd), NULL);
        if (ret <= 0) {
            break;
        }

        cmd = be32_to_cpu(cmd);
        switch (cmd) {
        case CMD_GET_CAPABILITY: {
            ptm_cap cap = cpu_to_be64(0x3fff);
            qio_channel_write(ioc, (char *)&cap, sizeof(cap), &error_abort);
            break;
        }
        case CMD_INIT: {
            ptm_init init;
            qio_channel_read(ioc, (char *)&init.u.req, sizeof(init.u.req),
                              &error_abort);
            init.u.resp.tpm_result = 0;
            qio_channel_write(ioc, (char *)&init.u.resp, sizeof(init.u.resp),
                              &error_abort);
            break;
        }
        case CMD_SHUTDOWN: {
            ptm_res res = 0;
            qio_channel_write(ioc, (char *)&res, sizeof(res), &error_abort);
            qio_channel_close(s->tpm_ioc, &error_abort);
            g_thread_join(s->emu_tpm_thread);
            break;
        }
        case CMD_STOP: {
            ptm_res res = 0;
            qio_channel_write(ioc, (char *)&res, sizeof(res), &error_abort);
            break;
        }
        case CMD_SET_BUFFERSIZE: {
            ptm_setbuffersize sbs;
            qio_channel_read(ioc, (char *)&sbs.u.req, sizeof(sbs.u.req),
                             &error_abort);
            sbs.u.resp.buffersize = sbs.u.req.buffersize ?: cpu_to_be32(4096);
            sbs.u.resp.tpm_result = 0;
            sbs.u.resp.minsize = cpu_to_be32(128);
            sbs.u.resp.maxsize = cpu_to_be32(4096);
            qio_channel_write(ioc, (char *)&sbs.u.resp, sizeof(sbs.u.resp),
                              &error_abort);
            break;
        }
        case CMD_GET_TPMESTABLISHED: {
            ptm_est est = {
                .u.resp.bit = 0,
            };
            qio_channel_write(ioc, (char *)&est, sizeof(est), &error_abort);
            break;
        }
        case CMD_SET_LOCALITY: {
            ptm_loc loc;
            /* Note: this time it's not u.req / u.resp... */
            qio_channel_read(ioc, (char *)&loc, sizeof(loc), &error_abort);
            g_assert_cmpint(loc.u.req.loc, ==, 0);
            loc.u.resp.tpm_result = 0;
            qio_channel_write(ioc, (char *)&loc, sizeof(loc), &error_abort);
            break;
        }
        default:
            g_debug("unimplemented %u", cmd);
            g_assert_not_reached();
        }
    }

    object_unref(OBJECT(ioc));
    object_unref(OBJECT(lioc));
    return NULL;
}

static const uint8_t TPM_CMD[12] =
    "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

static void tpm_tis_test_check_localities(const void *data)
{
    uint8_t locty;
    uint8_t access;
    uint32_t ifaceid;
    uint32_t capability;
    uint32_t didvid;
    uint32_t rid;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES; locty++) {
        access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        capability = readl(TIS_REG(locty, TPM_TIS_REG_INTF_CAPABILITY));
        g_assert_cmpint(capability, ==, TPM_TIS_CAPABILITIES_SUPPORTED2_0);

        ifaceid = readl(TIS_REG(locty, TPM_TIS_REG_INTERFACE_ID));
        g_assert_cmpint(ifaceid, ==, TPM_TIS_IFACE_ID_SUPPORTED_FLAGS2_0);

        didvid = readl(TIS_REG(locty, TPM_TIS_REG_DID_VID));
        g_assert_cmpint(didvid, !=, 0);
        g_assert_cmpint(didvid, !=, 0xffffffff);

        rid = readl(TIS_REG(locty, TPM_TIS_REG_RID));
        g_assert_cmpint(rid, !=, 0);
        g_assert_cmpint(rid, !=, 0xffffffff);
    }
}

static void tpm_tis_test_check_access_reg(const void *data)
{
    uint8_t locty;
    uint8_t access;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* release access */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    }
}

/*
 * Test case for seizing access from a higher number locality
 */
static void tpm_tis_test_check_access_reg_seize(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* lower localities cannot seize access */
        for (l = 0; l < locty; l++) {
            /* lower locality is not active */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to request use from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            /* requesting use from 'l' was not possible;
               we must see REQUEST_USE and possibly PENDING_REQUEST */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged;
               we must see PENDING_REQUEST */
            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);
            /* seize from 'l' was not possible */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged */
            access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* on the next loop we will have a PENDING_REQUEST flag
               set for 'l' */
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }

        /* higher localities can 'seize' access but not 'request use';
           note: this will activate first l+1, then l+2 etc. */
        for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            /* try to 'request use' from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);

            /* requesting use from 'l' was not possible; we should see
               REQUEST_USE and may see PENDING_REQUEST */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'l-1' must be unchanged; we should always
               see PENDING_REQUEST from 'l' requesting access */
            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_SEIZE);

            /* seize from 'l' was possible */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* l - 1 should show that it has BEEN_SEIZED */
            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_BEEN_SEIZED |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* clear the BEEN_SEIZED flag and make sure it's gone */
            writeb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_BEEN_SEIZED);

            access = readb(TIS_REG(l - 1, TPM_TIS_REG_ACCESS));
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }

        /* PENDING_REQUEST will not be set if locty = 0 since all localities
           were active; in case of locty = 1, locality 0 will be active
           but no PENDING_REQUEST anywhere */
        if (locty <= 1) {
            pending_request_flag = 0;
        }

        /* release access from l - 1; this activates locty - 1 */
        l--;

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
        DPRINTF_ACCESS;
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    pending_request_flag |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* release this locality */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        /* no locality may be active now */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

/*
 * Test case for getting access when higher number locality relinquishes access
 */
static void tpm_tis_test_check_access_reg_release(const void *data)
{
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = TPM_TIS_NUM_LOCALITIES - 2; locty >= 0; locty--) {
        pending_request_flag = 0;

        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
        access = readb(TIS_REG(locty, TPM_TIS_REG_ACCESS));
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of all other localities */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (l == locty) {
                continue;
            }
            /* request use of locality 'l' -- we MUST see REQUEST USE and
               may see PENDING_REQUEST */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        /* release locality 'locty' */
        writeb(TIS_REG(locty, TPM_TIS_REG_ACCESS),
               TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        /* highest locality should now be active; release it and make sure the
           next higest locality is active afterwards */
        for (l = TPM_TIS_NUM_LOCALITIES - 2; l >= 0; l--) {
            if (l == locty) {
                continue;
            }
            /* 'l' should be active now */
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            /* 'l' relinquishes access */
            writeb(TIS_REG(l, TPM_TIS_REG_ACCESS),
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);
            access = readb(TIS_REG(l, TPM_TIS_REG_ACCESS));
            DPRINTF_ACCESS;
            if (l == 1 || (locty <= 1 && l == 2)) {
                pending_request_flag = 0;
            }
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}

/*
 * Test case for transmitting packets
 */
static void tpm_tis_test_check_transmit(const void *data)
{
    const TestState *s = data;
    uint8_t access;
    uint32_t sts;
    uint16_t bcount;
    size_t i;

    /* request use of locality 0 */
    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_REQUEST_USE);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);
    g_assert_cmpint(sts & TPM_TIS_STS_TPM_FAMILY_MASK, ==,
                    TPM_TIS_STS_TPM_FAMILY2_0);

    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, 128);

    writel(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_COMMAND_READY);
    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < sizeof(TPM_CMD); i++) {
        writeb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO), TPM_CMD[i]);
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==,
                            TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
    }
    /* start processing */
    writeb(TIS_REG(0, TPM_TIS_REG_STS), TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == ,
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    bcount = (sts >> 8) & 0xffff;

    /* read response */
    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = readb(TIS_REG(0, TPM_TIS_REG_DATA_FIFO));
        sts = readl(TIS_REG(0, TPM_TIS_REG_STS));
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* relinquish use of locality 0 */
    writeb(TIS_REG(0, TPM_TIS_REG_ACCESS), TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = readb(TIS_REG(0, TPM_TIS_REG_ACCESS));
}

int main(int argc, char **argv)
{
    int ret;
    char *args, *tmp_path = g_dir_make_tmp("qemu-tpm-tis-test.XXXXXX", NULL);
    GThread *thread;
    TestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);

    thread = g_thread_new(NULL, emu_ctrl_thread, &test);
    test_wait_cond(&test);

    args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-tis,tpmdev=dev",
        test.addr->u.q_unix.path);
    qtest_start(args);

    qtest_add_data_func("/tpm-tis/test_check_localities", &test,
                        tpm_tis_test_check_localities);

    qtest_add_data_func("/tpm-tis/test_check_access_reg", &test,
                        tpm_tis_test_check_access_reg);

    qtest_add_data_func("/tpm-tis/test_check_access_reg_seize", &test,
                        tpm_tis_test_check_access_reg_seize);

    qtest_add_data_func("/tpm-tis/test_check_access_reg_release", &test,
                        tpm_tis_test_check_access_reg_release);

    qtest_add_data_func("/tpm-tis/test_check_transmit", &test,
                        tpm_tis_test_check_transmit);

    ret = g_test_run();

    qtest_end();

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    return ret;
}
