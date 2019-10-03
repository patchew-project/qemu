/*
 * qtest fw_cfg test case
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "standard-headers/linux/qemu_fw_cfg.h"
#include "libqos/fw_cfg.h"
#include "qemu/bswap.h"

static uint64_t ram_size = 128 << 20;
static uint16_t nb_cpus = 1;
static uint16_t max_cpus = 1;
static uint64_t nb_nodes = 0;
static uint16_t boot_menu = 0;

typedef struct {
    const char *machine_name;
    QFWCFG *fw_cfg;
} QTestCtx;

static void test_fw_cfg_signature(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;
    char buf[5];

    s = qtest_initf("-M %s", ctx->machine_name);

    qfw_cfg_get(s, ctx->fw_cfg, FW_CFG_SIGNATURE, buf, 4);
    buf[4] = 0;
    g_assert_cmpstr(buf, ==, "QEMU");

    qtest_quit(s);
}

static void test_fw_cfg_id(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;
    uint32_t id;

    s = qtest_initf("-M %s", ctx->machine_name);

    id = qfw_cfg_get_u32(s, ctx->fw_cfg, FW_CFG_ID);
    g_assert((id == 1) ||
             (id == 3));

    qtest_quit(s);
}

static void test_fw_cfg_uuid(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    uint8_t buf[16];
    static const uint8_t uuid[16] = {
        0x46, 0x00, 0xcb, 0x32, 0x38, 0xec, 0x4b, 0x2f,
        0x8a, 0xcb, 0x81, 0xc6, 0xea, 0x54, 0xf2, 0xd8,
    };

    s = qtest_initf("-M %s -uuid 4600cb32-38ec-4b2f-8acb-81c6ea54f2d8", ctx->machine_name);

    qfw_cfg_get(s, ctx->fw_cfg, FW_CFG_UUID, buf, 16);
    g_assert(memcmp(buf, uuid, sizeof(buf)) == 0);

    qtest_quit(s);

}

static void test_fw_cfg_ram_size(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u64(s, ctx->fw_cfg, FW_CFG_RAM_SIZE),
                    ==, ram_size);

    qtest_quit(s);
}

static void test_fw_cfg_nographic(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u16(s, ctx->fw_cfg, FW_CFG_NOGRAPHIC), ==, 0);

    qtest_quit(s);
}

static void test_fw_cfg_nb_cpus(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u16(s, ctx->fw_cfg, FW_CFG_NB_CPUS),
                    ==, nb_cpus);

    qtest_quit(s);
}

static void test_fw_cfg_max_cpus(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u16(s, ctx->fw_cfg, FW_CFG_MAX_CPUS),
                    ==, max_cpus);

    qtest_quit(s);
}

static void test_fw_cfg_numa(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;
    uint64_t *cpu_mask;
    uint64_t *node_mask;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u64(s, ctx->fw_cfg, FW_CFG_NUMA),
                    ==, nb_nodes);

    cpu_mask = g_new0(uint64_t, max_cpus);
    node_mask = g_new0(uint64_t, nb_nodes);

    qfw_cfg_read_data(s, ctx->fw_cfg, cpu_mask, sizeof(uint64_t) * max_cpus);
    qfw_cfg_read_data(s, ctx->fw_cfg, node_mask, sizeof(uint64_t) * nb_nodes);

    if (nb_nodes) {
        g_assert(cpu_mask[0] & 0x01);
        g_assert_cmpint(node_mask[0], ==, ram_size);
    }

    g_free(node_mask);
    g_free(cpu_mask);

    qtest_quit(s);
}

static void test_fw_cfg_boot_menu(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;

    s = qtest_initf("-M %s", ctx->machine_name);

    g_assert_cmpint(qfw_cfg_get_u16(s, ctx->fw_cfg, FW_CFG_BOOT_MENU),
                    ==, boot_menu);

    qtest_quit(s);
}

static void test_fw_cfg_reboot_timeout(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;
    uint32_t reboot_timeout = 0;
    size_t filesize;

    s = qtest_initf("-M %s -boot reboot-timeout=15", ctx->machine_name);

    filesize = qfw_cfg_get_file(s, ctx->fw_cfg, "etc/boot-fail-wait",
                                &reboot_timeout, sizeof(reboot_timeout));
    g_assert_cmpint(filesize, ==, sizeof(reboot_timeout));
    reboot_timeout = le32_to_cpu(reboot_timeout);
    g_assert_cmpint(reboot_timeout, ==, 15);

    qtest_quit(s);
}

static void test_fw_cfg_splash_time(const void *opaque)
{
    QTestCtx *ctx = (QTestCtx *)opaque;
    QTestState *s;
    uint16_t splash_time = 0;
    size_t filesize;

    s = qtest_initf("-M %s -boot splash-time=12", ctx->machine_name);

    filesize = qfw_cfg_get_file(s, ctx->fw_cfg, "etc/boot-menu-wait",
                                &splash_time, sizeof(splash_time));
    g_assert_cmpint(filesize, ==, sizeof(splash_time));
    splash_time = le16_to_cpu(splash_time);
    g_assert_cmpint(splash_time, ==, 12);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    QTestCtx *ctx = g_new(QTestCtx, 1);
    int ret;

    g_test_init(&argc, &argv, NULL);

    ctx->machine_name = "pc";
    ctx->fw_cfg = pc_fw_cfg_init();

    qtest_add_data_func("fw_cfg/signature", ctx, test_fw_cfg_signature);
    qtest_add_data_func("fw_cfg/id", ctx, test_fw_cfg_id);
    qtest_add_data_func("fw_cfg/uuid", ctx, test_fw_cfg_uuid);
    qtest_add_data_func("fw_cfg/ram_size", ctx, test_fw_cfg_ram_size);
    qtest_add_data_func("fw_cfg/nographic", ctx, test_fw_cfg_nographic);
    qtest_add_data_func("fw_cfg/nb_cpus", ctx, test_fw_cfg_nb_cpus);
#if 0
    qtest_add_func("fw_cfg/machine_id", test_fw_cfg_machine_id);
    qtest_add_func("fw_cfg/kernel", test_fw_cfg_kernel);
    qtest_add_func("fw_cfg/initrd", test_fw_cfg_initrd);
    qtest_add_func("fw_cfg/boot_device", test_fw_cfg_boot_device);
#endif
    qtest_add_data_func("fw_cfg/max_cpus", ctx, test_fw_cfg_max_cpus);
    qtest_add_data_func("fw_cfg/numa", ctx, test_fw_cfg_numa);
    qtest_add_data_func("fw_cfg/boot_menu", ctx, test_fw_cfg_boot_menu);
    qtest_add_data_func("fw_cfg/reboot_timeout", ctx,
                        test_fw_cfg_reboot_timeout);
    qtest_add_data_func("fw_cfg/splash_time", ctx, test_fw_cfg_splash_time);

    ret = g_test_run();

    g_free(ctx->fw_cfg);
    g_free(ctx);

    return ret;
}
