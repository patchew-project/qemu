/*
 * QTest for the ASPEED Caliptra mailbox on the ast1040-evb machine.
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/sockets.h"

/* AST1040 guest-visible mailbox bases. */
#define SRAM_BASE   0x21400000ULL
#define CSR_BASE    0x21600000ULL

#define CSR_LOCK         0x000
#define CSR_USER         0x004
#define CSR_CMD          0x010
#define CSR_DLEN         0x014
#define CSR_EXECUTE      0x018
#define CSR_CMD_STATUS   0x020

#define STATUS_BUSY        0
#define STATUS_COMPLETE    2
#define STATUS_CMD_FAILURE 3

/* SCU CPTRA page-select register (SCU base 0x74C02000 + 0x120). */
#define SCU_CPTRA_PAGE_REG0  0x74C02120ULL
/* MCI remap aperture. */
#define MCI_WINDOW           0x74200000ULL

/* Wire protocol. */
#define PROTO_MAGIC    0x4D424F58u
#define PROTO_VERSION  1u
#define CMD_EXECUTE    1u
#define CMD_RESPONSE   2u

static int peer_lfd = -1;
static int peer_fd = -1;

static void peer_read(void *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t r = read(peer_fd, (uint8_t *)buf + off, len - off);
        g_assert_cmpint(r, >, 0);
        off += r;
    }
}

static void peer_write(const void *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t w = write(peer_fd, (const uint8_t *)buf + off, len - off);
        g_assert_cmpint(w, >, 0);
        off += w;
    }
}

static uint32_t ld32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void st32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/*
 * Service one MBOX_EXECUTE: read the request, optionally check it, then reply
 * with MBOX_RESPONSE carrying @rsp_status and @rsp_data.
 */
static void peer_serve_execute(uint32_t expect_cmd, uint32_t expect_dlen,
                               uint32_t rsp_status,
                               const uint8_t *rsp_data, uint32_t rsp_dlen)
{
    uint8_t hdr[12];
    uint32_t plen, cmd, dlen;
    g_autofree uint8_t *payload = NULL;
    uint32_t rsp_padded = (rsp_dlen + 3) & ~3u;
    uint32_t rsp_plen = 8 + rsp_padded;
    g_autofree uint8_t *rsp = g_malloc0(12 + rsp_plen);

    peer_read(hdr, sizeof(hdr));
    g_assert_cmpuint(ld32(hdr), ==, PROTO_MAGIC);
    g_assert_cmpuint(hdr[4] | (hdr[5] << 8), ==, PROTO_VERSION);
    g_assert_cmpuint(hdr[6] | (hdr[7] << 8), ==, CMD_EXECUTE);
    plen = ld32(hdr + 8);
    g_assert_cmpuint(plen, >=, 8);

    payload = g_malloc(plen);
    peer_read(payload, plen);
    cmd = ld32(payload);
    dlen = ld32(payload + 4);
    g_assert_cmpuint(cmd, ==, expect_cmd);
    g_assert_cmpuint(dlen, ==, expect_dlen);

    st32(rsp, PROTO_MAGIC);
    rsp[4] = PROTO_VERSION; rsp[5] = 0;
    rsp[6] = CMD_RESPONSE;  rsp[7] = 0;
    st32(rsp + 8, rsp_plen);
    st32(rsp + 12, rsp_status);
    st32(rsp + 16, rsp_dlen);
    if (rsp_dlen) {
        memcpy(rsp + 20, rsp_data, rsp_dlen);
    }
    peer_write(rsp, 12 + rsp_plen);
}

static uint32_t poll_cmd_status(void)
{
    uint32_t s = STATUS_BUSY;
    int tries = 1000;

    while (tries--) {
        s = qtest_readl(global_qtest, CSR_BASE + CSR_CMD_STATUS);
        if (s != STATUS_BUSY) {
            break;
        }
    }
    return s;
}

/*
 * Realistic mailbox transaction: acquire the lock, run an EXECUTE roundtrip
 * against the peer, verify the response lands in SRAM, then release.
 */
static void test_execute(void)
{
    static const uint8_t resp[] = {
        0x11, 0xf9, 0xff, 0xff,   /* checksum */
        0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,   /* inner dlen = 20 */
        'C', 'a', 'l', 'i', 'p', 't', 'r', 'a',
        '_', 'C', 'o', 'r', 'e', '_', 'v', '2', '.', '0', '.', '0',
    };
    uint32_t status;

    /*
     * Acquire: first read returns 0, USER reflects the SoC agent, busy after.
     */
    g_assert_cmpuint(qtest_readl(global_qtest, CSR_BASE + CSR_LOCK), ==, 0);
    g_assert_cmpuint(qtest_readl(global_qtest, CSR_BASE + CSR_USER), ==, 1);
    g_assert_cmpuint(qtest_readl(global_qtest, CSR_BASE + CSR_LOCK), ==, 1);

    qtest_writel(global_qtest, SRAM_BASE + 0, 0xfffffec0);
    qtest_writel(global_qtest, CSR_BASE + CSR_CMD, 0x4d465756);
    qtest_writel(global_qtest, CSR_BASE + CSR_DLEN, 8);
    qtest_writel(global_qtest, CSR_BASE + CSR_EXECUTE, 1);

    peer_serve_execute(0x4d465756, 8, STATUS_COMPLETE, resp, sizeof(resp));

    status = poll_cmd_status();
    g_assert_cmpuint(status, ==, STATUS_COMPLETE);
    g_assert_cmpuint(qtest_readl(global_qtest, CSR_BASE + CSR_DLEN),
                     ==, sizeof(resp));
    g_assert_cmpuint(qtest_readl(global_qtest, SRAM_BASE + 0), ==, 0xfffff911);
    g_assert_cmpuint(qtest_readl(global_qtest, SRAM_BASE + 8), ==, 0x14);
    g_assert_cmpuint(qtest_readl(global_qtest, SRAM_BASE + 12), ==, 0x696c6143);

    /* Completing the transaction (EXECUTE 1->0) releases the lock. */
    qtest_writel(global_qtest, CSR_BASE + CSR_EXECUTE, 0);
    g_assert_cmpuint(qtest_readl(global_qtest, CSR_BASE + CSR_LOCK), ==, 0);
    qtest_writel(global_qtest, CSR_BASE + CSR_EXECUTE, 0);
}

/* SCU CPTRA_PAGE_REG0 selects the target page seen through the MCI window. */
static void test_scu_remap(void)
{
    /* Window pointed at the CSR page exposes the CMD register. */
    qtest_writel(global_qtest, CSR_BASE + CSR_CMD, 0xdeadbeef);
    qtest_writel(global_qtest, SCU_CPTRA_PAGE_REG0, CSR_BASE);
    g_assert_cmpuint(qtest_readl(global_qtest, MCI_WINDOW + CSR_CMD),
                     ==, 0xdeadbeef);

    /* Re-point at the SRAM page. */
    qtest_writel(global_qtest, SRAM_BASE + 0, 0x12345678);
    qtest_writel(global_qtest, SCU_CPTRA_PAGE_REG0, SRAM_BASE);
    g_assert_cmpuint(qtest_readl(global_qtest, MCI_WINDOW + 0),
                     ==, 0x12345678);
}

int main(int argc, char **argv)
{
    g_autofree char *sock_path = NULL;
    g_autofree char *cmdline = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    sock_path = g_strdup_printf("%s/cptra-mbox-%u.sock",
                                g_get_tmp_dir(), getpid());
    unlink(sock_path);
    peer_lfd = qtest_socket_server(sock_path);

    cmdline = g_strdup_printf(
        "-machine ast1040-evb,cptra-peer=peer0 "
        "-chardev socket,id=cptra0,path=%s "
        "-device cptra-mbox-peer-extern,id=peer0,chardev=cptra0", sock_path);
    qtest_start(cmdline);

    peer_fd = accept(peer_lfd, NULL, NULL);
    g_assert_cmpint(peer_fd, >=, 0);

    qtest_add_func("/cptra-mbox/execute", test_execute);
    qtest_add_func("/cptra-mbox/scu-remap", test_scu_remap);

    ret = g_test_run();

    qtest_end();
    if (peer_fd >= 0) {
        close(peer_fd);
    }
    close(peer_lfd);
    unlink(sock_path);

    return ret;
}
