/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Same-build Hexagon migration and internal snapshot regression tests. */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "migration/hexagon/guest.h"
#include "migration/hexagon/state.h"
#include "migration/migration-qmp.h"

#define TIMEOUT_US (30 * G_USEC_PER_SEC)

/* QTimer registers; each frame is 0x1000 bytes after the previous one. */
#define QTIMER_CNTPCT_LO        0x000
#define QTIMER_CNTP_CVAL_LO(f)  (0x020 + (f) * 0x1000)
#define QTIMER_CNTP_CTL(f)      (0x02c + (f) * 0x1000)
#define QTIMER_CTL_ENABLE       1
#define QTIMER_CTL_EXPIRED      5       /* enabled, IMASK clear, ISTATUS set */

/* L2VIC registers and the interrupt lines this test looks at. */
#define L2VIC_INT_TYPEn         0x280
#define L2VIC_SOFT_INTn         0x480
#define L2VIC_INT_PENDINGn      0x500
#define L2VIC_QTIMER0_IRQ       2
#define L2VIC_QTIMER1_IRQ       3
#define L2VIC_SOFT_IRQ          10
#define PENDING_TIMER0_SOFT \
    ((1 << L2VIC_QTIMER0_IRQ) | (1 << L2VIC_SOFT_IRQ))
#define PENDING_ALL (PENDING_TIMER0_SOFT | (1 << L2VIC_QTIMER1_IRQ))

static char *tmpdir;
static char *guest_path;

static uint32_t mailbox_read(QTestState *vm, unsigned offset)
{
    return qtest_readl(vm, STATE_MAILBOX + offset);
}

static void wait_word(QTestState *vm, uint64_t addr, uint32_t value)
{
    int64_t deadline = g_get_monotonic_time() + TIMEOUT_US;

    while (qtest_readl(vm, addr) != value) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static uint32_t qtimer_ctl(QTestState *vm, unsigned frame)
{
    return qtest_readl(vm, STATE_QTIMER + QTIMER_CNTP_CTL(frame));
}

static uint32_t l2vic_pending(QTestState *vm, uint32_t mask)
{
    return qtest_readl(vm, STATE_L2VIC + L2VIC_INT_PENDINGn) & mask;
}

static uint64_t qtimer_deadline(QTestState *vm)
{
    return qtest_readq(vm, STATE_QTIMER + QTIMER_CNTP_CVAL_LO(1));
}

static uint64_t qtimer_count(QTestState *vm)
{
    return qtest_readq(vm, STATE_QTIMER + QTIMER_CNTPCT_LO);
}

static void stop_vm(QTestState *vm)
{
    qtest_qmp_assert_success(vm, "{'execute':'stop'}");
    qtest_qmp_eventwait(vm, "STOP");
}

static QTestState *start_vm(const char *extra)
{
    return qtest_initf("-machine sm8975-nsp -m 16M -smp 2 "
                       "-accel tcg,thread=single -icount shift=0,sleep=off "
                       "-device loader,file=%s,addr=0x%x,cpu-num=0,"
                       "force-raw=on %s", guest_path, STATE_KERNEL_ADDR, extra);
}

static void wait_checkpoint(QTestState *vm)
{
    int64_t deadline = g_get_monotonic_time() + TIMEOUT_US;
    g_autofree char *pc = g_strdup_printf("pc = 0x%08x",
                                         HEXAGON_PEER_LOCK_PC);

    wait_word(vm, STATE_MAILBOX + STATE_READY, 1);
    wait_word(vm, STATE_MAILBOX + STATE_PEER_READY, 1);
    /* Wait until the second CPU has reached the blocking lock instruction. */
    while (true) {
        QDict *reply = qtest_qmp(vm,
            "{'execute':'human-monitor-command','arguments':"
            "{'command-line':'info registers','cpu-index':1}}");
        const char *output;
        bool found;

        g_assert_true(qdict_haskey(reply, "return"));
        output = qdict_get_str(reply, "return");
        found = strstr(output, pc) != NULL;

        if (!found && g_get_monotonic_time() >= deadline) {
            g_error("Expected %s in registers: %s", pc, output);
        }
        qobject_unref(reply);
        if (found) {
            break;
        }
        g_usleep(1000);
    }
    g_assert_cmphex(mailbox_read(vm, STATE_PEER_DONE), ==, 0);
}

static void arm_future_timer(QTestState *vm)
{
    uint64_t now = qtimer_count(vm);

    /* Frame 1 has a deadline still in the future at the checkpoint. */
    qtest_writel(vm, STATE_QTIMER + QTIMER_CNTP_CTL(1), 0);
    qtest_writeq(vm, STATE_QTIMER + QTIMER_CNTP_CVAL_LO(1), now + 1024);
    qtest_writel(vm, STATE_QTIMER + QTIMER_CNTP_CTL(1), QTIMER_CTL_ENABLE);
    g_assert_cmphex(qtimer_ctl(vm, 1), ==, QTIMER_CTL_ENABLE);
}

static void prepare_devices(QTestState *vm)
{
    /* Dispatch frame 0's expiry callback before freezing the clock. */
    qtest_writeq(vm, STATE_QTIMER + QTIMER_CNTP_CVAL_LO(0), 0);
    qtest_writel(vm, STATE_QTIMER + QTIMER_CNTP_CTL(0), QTIMER_CTL_ENABLE);
    qtest_qmp_assert_success(vm, "{'execute':'cont'}");
    wait_word(vm, STATE_QTIMER + QTIMER_CNTP_CTL(0), QTIMER_CTL_EXPIRED);
    stop_vm(vm);
    arm_future_timer(vm);
    /* Keep an independent software interrupt pending alongside the timer. */
    qtest_writel(vm, STATE_L2VIC + L2VIC_INT_TYPEn, 1 << L2VIC_SOFT_IRQ);
    qtest_writel(vm, STATE_L2VIC + L2VIC_SOFT_INTn, 1 << L2VIC_SOFT_IRQ);
    g_assert_cmphex(qtimer_ctl(vm, 0), ==, QTIMER_CTL_EXPIRED);
    g_assert_cmphex(l2vic_pending(vm, PENDING_TIMER0_SOFT), ==,
                    PENDING_TIMER0_SOFT);
}

static void check_devices(QTestState *vm, uint64_t now, uint64_t compare)
{
    g_assert_cmphex(qtest_readl(vm, STATE_L2VIC + L2VIC_INT_TYPEn), ==,
                    1 << L2VIC_SOFT_IRQ);
    g_assert_cmphex(qtimer_count(vm), ==, now);
    g_assert_cmphex(qtimer_deadline(vm), ==, compare);
    g_assert_cmphex(qtimer_ctl(vm, 0), ==, QTIMER_CTL_EXPIRED);
    g_assert_cmphex(qtimer_ctl(vm, 1), ==, QTIMER_CTL_ENABLE);
    g_assert_cmphex(l2vic_pending(vm, PENDING_TIMER0_SOFT), ==,
                    PENDING_TIMER0_SOFT);
}

static void check_bytes(QTestState *vm, uint64_t addr, size_t size,
                        uint16_t pattern)
{
    g_autofree uint8_t *data = g_malloc(size);

    qtest_memread(vm, addr, data, size);
    for (size_t i = 0; i < size; i++) {
        g_assert_cmphex(data[i], ==, (pattern >> (8 * (i & 1))) & 0xff);
    }
}

static void check_guest(QTestState *vm)
{
    qtest_writel(vm, STATE_MAILBOX + STATE_RELEASE, 1);
    qtest_qmp_assert_success(vm, "{'execute':'cont'}");
    wait_word(vm, STATE_MAILBOX + STATE_DONE, 1);
    wait_word(vm, STATE_MAILBOX + STATE_PEER_DONE, 1);
    /* Check that the migrated deadline fires after resuming. */
    wait_word(vm, STATE_QTIMER + QTIMER_CNTP_CTL(1), QTIMER_CTL_EXPIRED);
    stop_vm(vm);
    g_assert_cmphex(mailbox_read(vm, STATE_R20), ==, 0xdeadbeef);
    g_assert_cmphex(mailbox_read(vm, STATE_R21), ==, 0x76543210);
    g_assert_cmphex(mailbox_read(vm, STATE_PREDICATES), ==, 0xa55aff00);
    g_assert_cmphex(mailbox_read(vm, STATE_SSR), ==, 0x88020000);
    g_assert_cmphex(mailbox_read(vm, STATE_CCR), ==, 0x10200000);
    g_assert_cmphex(mailbox_read(vm, STATE_SGP0), ==, 0x31415926);
    g_assert_cmphex(mailbox_read(vm, STATE_MEMORY), ==, 0xdeadbeef);
    g_assert_cmphex(mailbox_read(vm, STATE_PEER_R22), ==, 0xcafef00d);
    check_bytes(vm, STATE_HVX1, 128, 0x4646);
    check_bytes(vm, STATE_HVX1 + 128, 128, 0x6868);
    check_bytes(vm, STATE_HVX1 + 256, 128, 0x6868);
    check_bytes(vm, STATE_HVX0, 128, 0x3535);
    check_bytes(vm, STATE_HVX0 + 128, 128, 0x5757);
    check_bytes(vm, STATE_HVX0 + 256, 128, 0x3535);
    check_bytes(vm, STATE_HLX1, 2048, 0x5678);
    check_bytes(vm, STATE_HLX0, 2048, 0x1234);
    g_assert_cmphex(l2vic_pending(vm, PENDING_ALL), ==, PENDING_ALL);
}

static void test_migration(gconstpointer opaque)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migrate.sock", tmpdir);
    QTestState *source = start_vm("");
    QTestState *dest;
    bool live = GPOINTER_TO_UINT(opaque);
    uint64_t now, compare;
    uint32_t heartbeat;

    wait_checkpoint(source);
    stop_vm(source);
    prepare_devices(source);
    now = qtimer_count(source);
    compare = qtimer_deadline(source);
    heartbeat = mailbox_read(source, STATE_HEARTBEAT);
    if (live) {
        migrate_set_capability(source, "pause-before-switchover", true);
        qtest_qmp_assert_success(source, "{'execute':'cont'}");
    }
    dest = start_vm("-incoming defer -S");
    migrate_incoming_qmp(dest, uri, NULL, "{}");
    migrate_qmp(source, dest, uri, NULL, "{}");
    if (live) {
        QDict *status;

        wait_for_migration_status(source, "pre-switchover", NULL);
        status = qtest_qmp_assert_success_ref(source,
                                             "{'execute':'query-status'}");
        g_assert_false(qdict_get_bool(status, "running"));
        qobject_unref(status);
        qtest_qmp_eventwait(source, "STOP");
        g_assert_cmpuint(mailbox_read(source, STATE_HEARTBEAT), >, heartbeat);
        arm_future_timer(source);
        now = qtimer_count(source);
        compare = qtimer_deadline(source);
        heartbeat = mailbox_read(source, STATE_HEARTBEAT);
        migrate_continue(source, "pre-switchover");
    }
    wait_for_migration_complete(source);
    wait_for_migration_complete(dest);
    /* Kill the source before checking state or allowing the guest to finish. */
    qtest_quit(source);
    check_devices(dest, now, compare);
    g_assert_cmphex(mailbox_read(dest, STATE_HEARTBEAT), ==, heartbeat);
    check_guest(dest);
    qtest_quit(dest);
}

static void snapshot_command(QTestState *vm, const char *command)
{
    g_autofree char *output = qtest_hmp(vm, "%s checkpoint", command);

    g_assert_cmpstr(output, ==, "");
}

static void test_snapshot(void)
{
    g_autofree char *disk = g_strdup_printf("%s/state.qcow2", tmpdir);
    g_autofree char *args = g_strdup_printf(
        "-drive if=none,id=state,file=%s,format=qcow2", disk);
    QTestState *vm;
    uint64_t now, compare;
    uint32_t heartbeat;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for qcow2 snapshot testing");
        return;
    }
    g_assert_true(mkimg(disk, "qcow2", 8));
    vm = start_vm(args);
    wait_checkpoint(vm);
    stop_vm(vm);
    prepare_devices(vm);
    now = qtimer_count(vm);
    compare = qtimer_deadline(vm);
    heartbeat = mailbox_read(vm, STATE_HEARTBEAT);
    snapshot_command(vm, "savevm");
    /* Change registers, memory, context selection and lock state. */
    check_guest(vm);
    qtest_writel(vm, STATE_MAILBOX + STATE_MEMORY, 0);
    snapshot_command(vm, "loadvm");
    g_assert_cmphex(qtest_readl(vm, STATE_MAILBOX), ==, 0);
    g_assert_cmphex(mailbox_read(vm, STATE_PEER_DONE), ==, 0);
    g_assert_cmphex(mailbox_read(vm, STATE_DONE), ==, 0);
    g_assert_cmphex(mailbox_read(vm, STATE_MEMORY), ==, 0xdeadbeef);
    g_assert_cmphex(mailbox_read(vm, STATE_HEARTBEAT), ==, heartbeat);
    check_devices(vm, now, compare);
    check_guest(vm);
    snapshot_command(vm, "delvm");
    qtest_quit(vm);
    unlink(disk);
}

int main(int argc, char **argv)
{
    int result;

    g_test_init(&argc, &argv, NULL);
    if (qtest_has_accel("tcg")) {
        tmpdir = g_dir_make_tmp("hexagon-migration-XXXXXX", NULL);
        g_assert_nonnull(tmpdir);
        guest_path = g_strdup_printf("%s/guest.bin", tmpdir);
        g_assert_true(g_file_set_contents(guest_path, (const char *)guest_bin,
                                          sizeof(guest_bin), NULL));
        qtest_add_data_func("/migration/stopped", GUINT_TO_POINTER(false),
                            test_migration);
        qtest_add_data_func("/migration/precopy", GUINT_TO_POINTER(true),
                            test_migration);
        qtest_add_func("/migration/snapshot", test_snapshot);
    }
    result = g_test_run();
    if (tmpdir) {
        unlink(guest_path);
        g_free(guest_path);
        g_assert_cmpint(rmdir(tmpdir), ==, 0);
        g_free(tmpdir);
    }
    return result;
}
