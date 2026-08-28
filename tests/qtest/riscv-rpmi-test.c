/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTests for RISC-V RPMI devices.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "libqtest.h"
#include "qobject/qdict.h"

#define RPMI_SHMEM_BASE 0x10200000ULL
#define RPMI_DOORBELL_BASE 0x10230000ULL
#define RPMI_SLOT_SIZE 64

#define RPMI_A2P_HEAD RPMI_SHMEM_BASE
#define RPMI_A2P_TAIL (RPMI_SHMEM_BASE + RPMI_SLOT_SIZE)
#define RPMI_A2P_SLOT0 (RPMI_SHMEM_BASE + 2 * RPMI_SLOT_SIZE)

#define RPMI_SRVGRP_BASE 0x0001
#define RPMI_SRVGRP_SYSTEM_RESET 0x0003
#define RPMI_SRVGRP_SYSTEM_SUSPEND 0x0004
#define RPMI_SRVGRP_HSM 0x0005
#define RPMI_BASE_SRV_GET_PLATFORM_INFO 0x05
#define RPMI_BASE_SRV_PROBE_SERVICE_GROUP 0x06
#define RPMI_SYSRST_SRV_GET_ATTRIBUTES 0x02
#define RPMI_SYSRST_SRV_SYSTEM_RESET 0x03
#define RPMI_HSM_SRV_GET_HART_STATUS 0x02
#define RPMI_HSM_SRV_GET_HART_LIST 0x03
#define RPMI_HSM_SRV_GET_SUSPEND_TYPES 0x04
#define RPMI_HSM_SRV_GET_SUSPEND_INFO 0x05
#define RPMI_HSM_SRV_HART_START 0x06
#define RPMI_HSM_SRV_HART_STOP 0x07
#define RPMI_HSM_SRV_HART_SUSPEND 0x08
#define RPMI_SYSSUSP_SRV_GET_ATTRIBUTES 0x02
#define RPMI_SYSSUSP_SRV_SYSTEM_SUSPEND 0x03
#define RPMI_MSG_NORMAL_REQUEST 0x00
#define RPMI_MSG_POSTED_REQUEST 0x01
#define RPMI_MSG_ACKNOWLEDGEMENT 0x02
#define RPMI_SYSRST_TYPE_SHUTDOWN 0x00
#define RPMI_SYSRST_TYPE_COLD_REBOOT 0x01
#define RPMI_SYSRST_TYPE_INVALID 0x03
#define RPMI_SYSRST_ATTRS_FLAGS_RESETTYPE 1
#define RPMI_TOKEN 0x55aa
#define RPMI_ERR_NOTSUPP 0xfffffffeU
#define RPMI_ERR_INVALID_PARAM 0xfffffffdU
#define RPMI_ERR_INVALID_ADDR 0xfffffffbU
#define RPMI_ERR_DENIED 0xfffffffcU
#define RPMI_HSM_HART_STATE_STARTED 0x00
#define RPMI_HSM_HART_STATE_STOPPED 0x01
#define RPMI_HSM_HART_STATE_SUSPENDED 0x04
#define RPMI_HSM_TEST_START_ADDR 0x80000000ULL
#define RPMI_HSM_TEST_RESUME_ADDR 0x80001000ULL

#define RPMI_P2A_ACK_BASE (RPMI_SHMEM_BASE + 16 * RPMI_SLOT_SIZE)
#define RPMI_P2A_ACK_HEAD RPMI_P2A_ACK_BASE
#define RPMI_P2A_ACK_TAIL (RPMI_P2A_ACK_BASE + RPMI_SLOT_SIZE)
#define RPMI_P2A_ACK_SLOT0 (RPMI_P2A_ACK_BASE + 2 * RPMI_SLOT_SIZE)

static uint64_t rpmi_response_base;

static uint64_t rpmi_queue_slot(uint64_t queue_base, uint32_t index)
{
    return queue_base + (index + 2) * RPMI_SLOT_SIZE;
}

static void rpmi_send_request(QTestState *qts, uint16_t service_group,
                              uint8_t service_id, uint8_t request_type,
                              const uint32_t *data, size_t data_words)
{
    uint32_t tail = qtest_readl(qts, RPMI_A2P_TAIL);
    uint64_t slot = rpmi_queue_slot(RPMI_SHMEM_BASE, tail);
    size_t i;

    qtest_writew(qts, slot, service_group);
    qtest_writeb(qts, slot + 2, service_id);
    qtest_writeb(qts, slot + 3, request_type);
    qtest_writew(qts, slot + 4, data_words * sizeof(*data));
    qtest_writew(qts, slot + 6, RPMI_TOKEN);

    for (i = 0; i < data_words; i++) {
        qtest_writel(qts, slot + 8 + i * sizeof(*data), data[i]);
    }

    g_test_message(
        "RPMI_A2P_REQ shmem=0x%016" PRIx64 " doorbell=0x%016" PRIx64
        " group=0x%04x service=0x%02x type=0x%02x data_len=%zu"
        " token=0x%04x a2p_tail=%u slot=0x%016" PRIx64,
        (uint64_t)RPMI_SHMEM_BASE, (uint64_t)RPMI_DOORBELL_BASE,
        service_group, service_id, request_type, data_words * sizeof(*data),
        RPMI_TOKEN, tail, slot);

    qtest_writel(qts, RPMI_A2P_TAIL, (tail + 1) % 16);
    qtest_writel(qts, RPMI_DOORBELL_BASE, 1);
}

static uint32_t rpmi_response_word(QTestState *qts, unsigned int word)
{
    return qtest_readl(qts, rpmi_response_base + 8 + word * sizeof(uint32_t));
}

static void rpmi_expect_ack(QTestState *qts, uint16_t service_group,
                            uint8_t service_id, uint16_t data_len)
{
    uint32_t head = qtest_readl(qts, RPMI_P2A_ACK_HEAD);
    uint32_t tail = qtest_readl(qts, RPMI_P2A_ACK_TAIL);

    g_assert_cmphex(tail, !=, head);
    rpmi_response_base = rpmi_queue_slot(RPMI_P2A_ACK_BASE, head);
    g_assert_cmphex(qtest_readw(qts, rpmi_response_base), ==, service_group);
    g_assert_cmphex(qtest_readb(qts, rpmi_response_base + 2), ==,
                    service_id);
    g_assert_cmphex(qtest_readb(qts, rpmi_response_base + 3), ==,
                    RPMI_MSG_ACKNOWLEDGEMENT);
    g_assert_cmphex(qtest_readw(qts, rpmi_response_base + 4), ==, data_len);
    g_assert_cmphex(qtest_readw(qts, rpmi_response_base + 6), ==,
                    RPMI_TOKEN);
    g_test_message(
        "RPMI_P2A_ACK shmem=0x%016" PRIx64
        " group=0x%04x service=0x%02x type=0x%02x data_len=%u"
        " token=0x%04x p2a_head=%u slot=0x%016" PRIx64
        " status=0x%08x",
        (uint64_t)RPMI_SHMEM_BASE, service_group, service_id,
        RPMI_MSG_ACKNOWLEDGEMENT, data_len, RPMI_TOKEN, head,
        rpmi_response_base,
        data_len >= sizeof(uint32_t) ? rpmi_response_word(qts, 0) : 0);
    qtest_writel(qts, RPMI_P2A_ACK_HEAD, (head + 1) % 16);
}

static void rpmi_send_sysreset(QTestState *qts, uint32_t reset_type,
                               uint8_t request_type)
{
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_RESET,
                      RPMI_SYSRST_SRV_SYSTEM_RESET, request_type,
                      &reset_type, 1);
}

static void rpmi_expect_qemu_failure(const char *extra_args,
                                     const char *stderr_needle)
{
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) split_args = NULL;
    g_autoptr(GPtrArray) argv = g_ptr_array_new();
    g_autofree char *stderr_data = NULL;
    gint wait_status;
    gboolean spawned;

    g_assert_true(g_shell_parse_argv(extra_args, NULL, &split_args, &error));
    g_assert_no_error(error);

    g_ptr_array_add(argv, (gpointer)qtest_qemu_binary(NULL));
    for (char **arg = split_args; *arg; arg++) {
        g_ptr_array_add(argv, *arg);
    }
    g_ptr_array_add(argv, NULL);

    spawned = g_spawn_sync(NULL, (char **)argv->pdata, NULL,
                           G_SPAWN_STDOUT_TO_DEV_NULL,
                           NULL, NULL, NULL, &stderr_data,
                           &wait_status, &error);
    g_assert_true(spawned);
    g_assert_no_error(error);
    g_assert_false(g_spawn_check_exit_status(wait_status, NULL));
    g_assert_nonnull(stderr_data);
    g_assert_nonnull(strstr(stderr_data, stderr_needle));
}

static void test_rpmi_machine_realize_off(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=off");
    qtest_quit(qts);
}

static void test_rpmi_machine_rejects_too_many_harts(void)
{
    rpmi_expect_qemu_failure(
        "-machine virt,rpmi=on -smp 513 -display none -S",
        "max CPUs supported by machine 'virt' is 512");
}

static void test_rpmi_base_platform_info(void)
{
    static const char expected[] = "QEMU RISC-V virt RPMI";
    QTestState *qts;
    size_t i;

    qts = qtest_init("-machine virt,rpmi=on");
    rpmi_send_request(qts, RPMI_SRVGRP_BASE,
                      RPMI_BASE_SRV_GET_PLATFORM_INFO,
                      RPMI_MSG_NORMAL_REQUEST, NULL, 0);

    rpmi_expect_ack(qts, RPMI_SRVGRP_BASE,
                    RPMI_BASE_SRV_GET_PLATFORM_INFO,
                    2 * sizeof(uint32_t) + sizeof(expected));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, sizeof(expected));
    for (i = 0; i < sizeof(expected); i++) {
        g_assert_cmphex(qtest_readb(qts, RPMI_P2A_ACK_SLOT0 + 16 + i), ==,
                        expected[i]);
    }

    qtest_quit(qts);
}

static void rpmi_probe_group(QTestState *qts, uint32_t service_group,
                             bool present)
{
    rpmi_send_request(qts, RPMI_SRVGRP_BASE,
                      RPMI_BASE_SRV_PROBE_SERVICE_GROUP,
                      RPMI_MSG_NORMAL_REQUEST, &service_group, 1);

    rpmi_expect_ack(qts, RPMI_SRVGRP_BASE,
                    RPMI_BASE_SRV_PROBE_SERVICE_GROUP,
                    2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    if (present) {
        g_assert_cmphex(rpmi_response_word(qts, 1), !=, 0);
    } else {
        g_assert_cmphex(rpmi_response_word(qts, 1), ==, 0);
    }
}

static void test_rpmi_base_probe_service_groups(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on,aia=aplic-imsic");
    rpmi_probe_group(qts, RPMI_SRVGRP_BASE, true);
    qtest_system_reset(qts);
    rpmi_probe_group(qts, RPMI_SRVGRP_SYSTEM_RESET, true);
    qtest_system_reset(qts);
    rpmi_probe_group(qts, RPMI_SRVGRP_HSM, true);
    qtest_system_reset(qts);
    rpmi_probe_group(qts, RPMI_SRVGRP_SYSTEM_SUSPEND, true);

    qtest_quit(qts);
}

static void test_rpmi_sysreset_attrs(void)
{
    QTestState *qts;
    uint32_t reset_type = RPMI_SYSRST_TYPE_SHUTDOWN;

    qts = qtest_init("-machine virt,rpmi=on");
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_RESET,
                      RPMI_SYSRST_SRV_GET_ATTRIBUTES,
                      RPMI_MSG_NORMAL_REQUEST, &reset_type, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                    RPMI_SYSRST_SRV_GET_ATTRIBUTES, 2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==,
                    RPMI_SYSRST_ATTRS_FLAGS_RESETTYPE);

    qtest_system_reset(qts);
    reset_type = RPMI_SYSRST_TYPE_COLD_REBOOT;
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_RESET,
                      RPMI_SYSRST_SRV_GET_ATTRIBUTES,
                      RPMI_MSG_NORMAL_REQUEST, &reset_type, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                    RPMI_SYSRST_SRV_GET_ATTRIBUTES, 2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==,
                    RPMI_SYSRST_ATTRS_FLAGS_RESETTYPE);

    qtest_system_reset(qts);
    reset_type = RPMI_SYSRST_TYPE_INVALID;
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_RESET,
                      RPMI_SYSRST_SRV_GET_ATTRIBUTES,
                      RPMI_MSG_NORMAL_REQUEST, &reset_type, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                    RPMI_SYSRST_SRV_GET_ATTRIBUTES, 2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, 0);

    qtest_quit(qts);
}

static void test_rpmi_sysreset_shutdown(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on");
    rpmi_send_sysreset(qts, RPMI_SYSRST_TYPE_SHUTDOWN,
                       RPMI_MSG_POSTED_REQUEST);
    qtest_qmp_eventwait(qts, "SHUTDOWN");
    qtest_quit(qts);
}

static void test_rpmi_sysreset_cold_reboot(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on -no-reboot");
    rpmi_send_sysreset(qts, RPMI_SYSRST_TYPE_COLD_REBOOT,
                       RPMI_MSG_POSTED_REQUEST);
    qtest_qmp_eventwait(qts, "SHUTDOWN");
    qtest_quit(qts);
}

static void test_rpmi_sysreset_invalid_type(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on");
    rpmi_send_sysreset(qts, RPMI_SYSRST_TYPE_INVALID,
                       RPMI_MSG_NORMAL_REQUEST);

    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                    RPMI_SYSRST_SRV_SYSTEM_RESET, sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==,
                    RPMI_ERR_INVALID_PARAM);

    qtest_quit(qts);
}

static void test_rpmi_repeated_reset_after_traffic(void)
{
    QTestState *qts;
    uint32_t reset_type = RPMI_SYSRST_TYPE_SHUTDOWN;

    qts = qtest_init("-machine virt,rpmi=on,aia=aplic-imsic");
    for (uint32_t i = 0; i < 5; i++) {
        rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_RESET,
                          RPMI_SYSRST_SRV_GET_ATTRIBUTES,
                          RPMI_MSG_NORMAL_REQUEST,
                          &reset_type, 1);
        rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                        RPMI_SYSRST_SRV_GET_ATTRIBUTES,
                        2 * sizeof(uint32_t));
        g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
        qtest_system_reset(qts);
    }
    qtest_quit(qts);
}

static void test_rpmi_reset_clears_transport(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on");
    rpmi_send_sysreset(qts, RPMI_SYSRST_TYPE_INVALID,
                       RPMI_MSG_NORMAL_REQUEST);
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_RESET,
                    RPMI_SYSRST_SRV_SYSTEM_RESET, sizeof(uint32_t));

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts, RPMI_A2P_TAIL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RPMI_P2A_ACK_TAIL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RPMI_DOORBELL_BASE), ==, 0);

    qtest_quit(qts);
}

static void test_rpmi_doorbell_invalid_access(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on");
    qtest_writeb(qts, RPMI_DOORBELL_BASE, 1);
    qtest_writel(qts, RPMI_DOORBELL_BASE + 4, 1);
    g_assert_cmphex(qtest_readl(qts, RPMI_DOORBELL_BASE), ==, 0);

    qtest_quit(qts);
}

static void test_rpmi_queue_bounds(void)
{
    QTestState *qts;

    qts = qtest_init("-machine virt,rpmi=on");
    qtest_writel(qts, RPMI_A2P_TAIL, 0x1000);
    qtest_writel(qts, RPMI_DOORBELL_BASE, 1);
    g_assert_cmphex(qtest_readl(qts, RPMI_A2P_HEAD), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RPMI_P2A_ACK_TAIL), ==, 0);

    qtest_quit(qts);
}

static void test_rpmi_migration_blocked(void)
{
    QTestState *qts;
    QDict *error;
    const char *desc;

    qts = qtest_init("-machine virt,rpmi=on -S");
    error = qtest_qmp_assert_failure_ref(qts,
        "{ 'execute': 'migrate',"
        "  'arguments': { 'uri': 'exec:cat > /dev/null' } }");
    desc = qdict_get_try_str(error, "desc");

    g_assert_nonnull(desc);
    g_assert_nonnull(strstr(desc, "non-migratable device"));
    g_assert_nonnull(strstr(desc, "riscv-rpmi"));

    qobject_unref(error);
    qtest_quit(qts);
}

static void test_rpmi_hsm_hart_list(void)
{
    QTestState *qts;
    uint32_t start_index = 0;

    qts = qtest_init("-machine virt,rpmi=on -smp 4");
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_LIST,
                      RPMI_MSG_NORMAL_REQUEST, &start_index, 1);

    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_LIST,
                    7 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 2), ==, 4);
    g_assert_cmphex(rpmi_response_word(qts, 3), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 4), ==, 1);
    g_assert_cmphex(rpmi_response_word(qts, 5), ==, 2);
    g_assert_cmphex(rpmi_response_word(qts, 6), ==, 3);

    qtest_quit(qts);
}

static void test_rpmi_hsm_multi_socket_hart_list(void)
{
    QTestState *qts;
    uint32_t start_index = 0;

    qts = qtest_init("-machine virt,rpmi=on "
                     "-smp 4,sockets=2,cores=2,threads=1");
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_LIST,
                      RPMI_MSG_NORMAL_REQUEST, &start_index, 1);

    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_LIST,
                    7 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 2), ==, 4);
    g_assert_cmphex(rpmi_response_word(qts, 3), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 4), ==, 1);
    g_assert_cmphex(rpmi_response_word(qts, 5), ==, 2);
    g_assert_cmphex(rpmi_response_word(qts, 6), ==, 3);

    qtest_quit(qts);
}

static void test_rpmi_hsm_hart_status(void)
{
    QTestState *qts;
    uint32_t hart_id = 3;

    qts = qtest_init("-machine virt,rpmi=on -smp 4");
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_STATUS,
                      RPMI_MSG_NORMAL_REQUEST, &hart_id, 1);

    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_STATUS,
                    2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==,
                    RPMI_HSM_HART_STATE_STARTED);

    qtest_quit(qts);
}

static void rpmi_expect_hsm_status(QTestState *qts, uint32_t hart_id,
                                   uint32_t expected_state)
{
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_STATUS,
                      RPMI_MSG_NORMAL_REQUEST, &hart_id, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_HART_STATUS,
                    2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, expected_state);
}

static uint64_t rpmi_hart_pc(QTestState *qts, uint32_t cpu_index)
{
    g_autofree char *registers = qtest_hmp(qts, "info registers %u",
                                           cpu_index);
    const char *pc_line;
    uint64_t pc;

    pc_line = strstr(registers, "\n pc");
    g_assert_nonnull(pc_line);
    g_assert_cmpint(sscanf(pc_line, "\n pc %" SCNx64, &pc), ==, 1);

    return pc;
}

static void test_rpmi_hsm_hart_control(void)
{
    QTestState *qts;
    uint32_t hart_id = 1;
    uint32_t stop_request[] = { hart_id };
    uint32_t start_request[] = { hart_id, RPMI_HSM_TEST_START_ADDR, 0 };
    uint32_t suspend_request[] = { hart_id, 0, RPMI_HSM_TEST_RESUME_ADDR, 0 };
    uint32_t start_index = 0;
    uint32_t suspend_type = 0;

    qts = qtest_init("-machine virt,rpmi=on -smp 2");
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_SUSPEND_TYPES,
                      RPMI_MSG_NORMAL_REQUEST, &start_index, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_SUSPEND_TYPES,
                    4 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 2), ==, 1);
    g_assert_cmphex(rpmi_response_word(qts, 3), ==, 0);

    qtest_system_reset(qts);
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_SUSPEND_INFO,
                      RPMI_MSG_NORMAL_REQUEST, &suspend_type, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_GET_SUSPEND_INFO,
                    6 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);

    qtest_system_reset(qts);
    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_STOP,
                      RPMI_MSG_NORMAL_REQUEST, stop_request,
                      ARRAY_SIZE(stop_request));
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_STOP,
                    sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    rpmi_expect_hsm_status(qts, hart_id, RPMI_HSM_HART_STATE_STOPPED);

    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_START,
                      RPMI_MSG_NORMAL_REQUEST, start_request,
                      ARRAY_SIZE(start_request));
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_START,
                    sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    rpmi_expect_hsm_status(qts, hart_id, RPMI_HSM_HART_STATE_STARTED);
    g_assert_cmphex(rpmi_hart_pc(qts, hart_id), ==, RPMI_HSM_TEST_START_ADDR);

    rpmi_send_request(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_SUSPEND,
                      RPMI_MSG_NORMAL_REQUEST, suspend_request,
                      ARRAY_SIZE(suspend_request));
    rpmi_expect_ack(qts, RPMI_SRVGRP_HSM, RPMI_HSM_SRV_HART_SUSPEND,
                    sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    rpmi_expect_hsm_status(qts, hart_id, RPMI_HSM_HART_STATE_SUSPENDED);

    qtest_quit(qts);
}

static void test_rpmi_syssusp_attrs_and_suspend(void)
{
    QTestState *qts;
    uint32_t suspend_type = 0;
    uint32_t suspend_request[] = { 0, 0, 0x80000000, 0 };

    qts = qtest_init("-machine virt,rpmi=on -smp 1");
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_SUSPEND,
                      RPMI_SYSSUSP_SRV_GET_ATTRIBUTES,
                      RPMI_MSG_NORMAL_REQUEST, &suspend_type, 1);
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_SUSPEND,
                    RPMI_SYSSUSP_SRV_GET_ATTRIBUTES,
                    2 * sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    g_assert_cmphex(rpmi_response_word(qts, 1), ==, 3);

    qtest_system_reset(qts);
    rpmi_send_request(qts, RPMI_SRVGRP_SYSTEM_SUSPEND,
                      RPMI_SYSSUSP_SRV_SYSTEM_SUSPEND,
                      RPMI_MSG_NORMAL_REQUEST, suspend_request,
                      ARRAY_SIZE(suspend_request));
    rpmi_expect_ack(qts, RPMI_SRVGRP_SYSTEM_SUSPEND,
                    RPMI_SYSSUSP_SRV_SYSTEM_SUSPEND, sizeof(uint32_t));
    g_assert_cmphex(rpmi_response_word(qts, 0), ==, 0);
    qtest_qmp_eventwait(qts, "SUSPEND");
    qtest_qmp_assert_success(qts, "{ 'execute': 'system_wakeup' }");
    qtest_qmp_eventwait(qts, "WAKEUP");

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/riscv/rpmi/machine/realize-off",
                       test_rpmi_machine_realize_off);
        qtest_add_func("/riscv/rpmi/machine/rejects-too-many-harts",
                       test_rpmi_machine_rejects_too_many_harts);
        qtest_add_func("/riscv/rpmi/base/platform-info",
                       test_rpmi_base_platform_info);
        qtest_add_func("/riscv/rpmi/base/probe-service-groups",
                       test_rpmi_base_probe_service_groups);
        qtest_add_func("/riscv/rpmi/sysreset/attrs",
                       test_rpmi_sysreset_attrs);
        qtest_add_func("/riscv/rpmi/sysreset/shutdown",
                       test_rpmi_sysreset_shutdown);
        qtest_add_func("/riscv/rpmi/sysreset/cold-reboot",
                       test_rpmi_sysreset_cold_reboot);
        qtest_add_func("/riscv/rpmi/sysreset/invalid-type",
                       test_rpmi_sysreset_invalid_type);
        qtest_add_func("/riscv/rpmi/reset/clears-transport",
                       test_rpmi_reset_clears_transport);
        qtest_add_func("/riscv/rpmi/negative/doorbell-invalid-access",
                       test_rpmi_doorbell_invalid_access);
        qtest_add_func("/riscv/rpmi/negative/queue-bounds",
                       test_rpmi_queue_bounds);
        qtest_add_func("/riscv/rpmi/reset/repeated-after-traffic",
                       test_rpmi_repeated_reset_after_traffic);
        qtest_add_func("/riscv/rpmi/migration/blocked",
                       test_rpmi_migration_blocked);
        qtest_add_func("/riscv/rpmi/hsm/hart-list",
                       test_rpmi_hsm_hart_list);
        qtest_add_func("/riscv/rpmi/hsm/multi-socket-hart-list",
                       test_rpmi_hsm_multi_socket_hart_list);
        qtest_add_func("/riscv/rpmi/hsm/hart-status",
                       test_rpmi_hsm_hart_status);
        qtest_add_func("/riscv/rpmi/hsm/hart-control",
                       test_rpmi_hsm_hart_control);
        qtest_add_func("/riscv/rpmi/syssusp/attrs-and-suspend",
                       test_rpmi_syssusp_attrs_and_suspend);
    }

    return g_test_run();
}
