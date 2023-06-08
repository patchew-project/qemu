/*
 * QTest migration helpers
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qjson.h"

#include "tests/migration/migration-test.h"
#include "migration-helpers.h"

#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#endif

/* For dirty ring test; so far only x86_64 is supported */
#if defined(__linux__) && defined(HOST_X86_64)
#include "linux/kvm.h"
#endif

char *tmpfs;
static char *bootpath;

/*
 * Number of seconds we wait when looking for migration
 * status changes, to avoid test suite hanging forever
 * when things go wrong. Needs to be higher enough to
 * avoid false positives on loaded hosts.
 */
#define MIGRATION_STATUS_WAIT_TIMEOUT 120

bool migrate_watch_for_stop(QTestState *who, const char *name,
                            QDict *event, void *opaque)
{
    bool *seen = opaque;

    if (g_str_equal(name, "STOP")) {
        *seen = true;
        return true;
    }

    return false;
}

bool migrate_watch_for_resume(QTestState *who, const char *name,
                              QDict *event, void *opaque)
{
    bool *seen = opaque;

    if (g_str_equal(name, "RESUME")) {
        *seen = true;
        return true;
    }

    return false;
}

/*
 * Send QMP command "migrate".
 * Arguments are built from @fmt... (formatted like
 * qobject_from_jsonf_nofail()) with "uri": @uri spliced in.
 */
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate', 'arguments': %p}", args);
}

/*
 * Note: caller is responsible to free the returned object via
 * qobject_unref() after use
 */
QDict *migrate_query(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who, "{ 'execute': 'query-migrate' }");
}

QDict *migrate_query_not_failed(QTestState *who)
{
    const char *status;
    QDict *rsp = migrate_query(who);
    status = qdict_get_str(rsp, "status");
    if (g_str_equal(status, "failed")) {
        g_printerr("query-migrate shows failed migration: %s\n",
                   qdict_get_str(rsp, "error-desc"));
    }
    g_assert(!g_str_equal(status, "failed"));
    return rsp;
}

/*
 * Note: caller is responsible to free the returned object via
 * g_free() after use
 */
static gchar *migrate_query_status(QTestState *who)
{
    QDict *rsp_return = migrate_query(who);
    gchar *status = g_strdup(qdict_get_str(rsp_return, "status"));

    g_assert(status);
    qobject_unref(rsp_return);

    return status;
}

static bool check_migration_status(QTestState *who, const char *goal,
                                   const char **ungoals)
{
    bool ready;
    char *current_status;
    const char **ungoal;

    current_status = migrate_query_status(who);
    ready = strcmp(current_status, goal) == 0;
    if (!ungoals) {
        g_assert_cmpstr(current_status, !=, "failed");
        /*
         * If looking for a state other than completed,
         * completion of migration would cause the test to
         * hang.
         */
        if (strcmp(goal, "completed") != 0) {
            g_assert_cmpstr(current_status, !=, "completed");
        }
    } else {
        for (ungoal = ungoals; *ungoal; ungoal++) {
            g_assert_cmpstr(current_status, !=,  *ungoal);
        }
    }
    g_free(current_status);
    return ready;
}

void wait_for_migration_status(QTestState *who,
                               const char *goal, const char **ungoals)
{
    g_test_timer_start();
    while (!check_migration_status(who, goal, ungoals)) {
        usleep(1000);

        g_assert(g_test_timer_elapsed() < MIGRATION_STATUS_WAIT_TIMEOUT);
    }
}

void wait_for_migration_complete(QTestState *who)
{
    wait_for_migration_status(who, "completed", NULL);
}

void wait_for_migration_fail(QTestState *from, bool allow_active)
{
    g_test_timer_start();
    QDict *rsp_return;
    char *status;
    bool failed;

    do {
        status = migrate_query_status(from);
        bool result = !strcmp(status, "setup") || !strcmp(status, "failed") ||
            (allow_active && !strcmp(status, "active"));
        if (!result) {
            fprintf(stderr, "%s: unexpected status status=%s allow_active=%d\n",
                    __func__, status, allow_active);
        }
        g_assert(result);
        failed = !strcmp(status, "failed");
        g_free(status);

        g_assert(g_test_timer_elapsed() < MIGRATION_STATUS_WAIT_TIMEOUT);
    } while (!failed);

    /* Is the machine currently running? */
    rsp_return = qtest_qmp_assert_success_ref(from,
                                              "{ 'execute': 'query-status' }");
    g_assert(qdict_haskey(rsp_return, "running"));
    g_assert(qdict_get_bool(rsp_return, "running"));
    qobject_unref(rsp_return);
}

/* The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "tests/migration/i386/a-b-bootblock.h"
#include "tests/migration/aarch64/a-b-kernel.h"
#include "tests/migration/s390x/a-b-bios.h"

void bootfile_create(char *dir)
{
    const char *arch = qtest_get_arch();
    unsigned char *content;
    size_t len;

    bootpath = g_strdup_printf("%s/bootsect", dir);
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        /* the assembled x86 boot sector should be exactly one sector large */
        g_assert(sizeof(x86_bootsect) == 512);
        content = x86_bootsect;
        len = sizeof(x86_bootsect);
    } else if (g_str_equal(arch, "s390x")) {
        content = s390x_elf;
        len = sizeof(s390x_elf);
    } else if (strcmp(arch, "ppc64") == 0) {
        /*
         * sane architectures can be programmed at the boot prompt
         */
        return;
    } else if (strcmp(arch, "aarch64") == 0) {
        content = aarch64_kernel;
        len = sizeof(aarch64_kernel);
        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
    } else {
        g_assert_not_reached();
    }

    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, len, 1, bootfile), ==, 1);
    fclose(bootfile);
}

void bootfile_delete(void)
{
    unlink(bootpath);
    g_free(bootpath);
    bootpath = NULL;
}

GuestState *guest_create(const char *name)
{
    GuestState *vm = g_new0(GuestState, 1);
    const char *arch = qtest_get_arch();

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        vm->memory_size = "150M";
        vm->arch_opts = g_strdup_printf("-drive file=%s,format=raw", bootpath);
        vm->start_address = X86_TEST_MEM_START;
        vm->end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        vm->memory_size = "128M";
        vm->arch_opts = g_strdup_printf("-bios %s", bootpath);
        vm->start_address = S390_TEST_MEM_START;
        vm->end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        vm->memory_size = "256M";
        vm->start_address = PPC_TEST_MEM_START;
        vm->end_address = PPC_TEST_MEM_END;
        vm->arch_source = g_strdup_printf(
            "-prom-env 'use-nvramrc?=true' -prom-env "
            "'nvramrc=hex .\" _\" begin %x %x "
            "do i c@ 1 + i c! 1000 +loop .\" B\" 0 "
            "until'", vm->end_address, vm->start_address);
        vm->arch_opts = g_strdup("-nodefaults -machine vsmt=8");
    } else if (strcmp(arch, "aarch64") == 0) {
        vm->memory_size = "150M";
        vm->arch_opts = g_strdup_printf(
            "-machine virt,gic-version=max -cpu max -kernel %s", bootpath);
        vm->start_address = ARM_TEST_MEM_START;
        vm->end_address = ARM_TEST_MEM_END;
    } else {
        g_assert_not_reached();
    }

    vm->name = name;
    vm->serial_path = g_strdup_printf("%s/%s", tmpfs, vm->name);
    return vm;
}

void guest_destroy(GuestState *vm)
{
    qtest_quit(vm->qs);
    g_free(vm->arch_opts);
    g_free(vm->arch_source);
    g_free(vm->arch_target);
    g_free(vm->kvm_opts);
    unlink(vm->serial_path);
    g_free(vm->serial_path);
    g_free(vm->shmem_opts);
    unlink(vm->shmem_path);
    g_free(vm->shmem_path);
    if (vm->unix_socket) {
        unlink(vm->unix_socket);
        g_free(vm->unix_socket);
    }
    g_free(vm->uri);
    g_free(vm);
}

void guest_realize(GuestState *who)
{
    bool target = false;
    if (strncmp(who->name, "target", strlen("target")) == 0) {
        target = true;
    }
    gchar *
    cmd = g_strdup_printf("-accel kvm%s -accel tcg "
                          "-name %s,debug-threads=on "
                          "-m %s "
                          "-serial file:%s "
                          "%s %s "
                          "%s %s %s %s %s",
                          who->kvm_opts ? who->kvm_opts : "",
                          who->name,
                          who->memory_size,
                          who->serial_path,
                          target ? "-incoming" : "",
                          target ? who->uri ? who->uri : "defer"
                                 : "",
                          who->arch_opts ? who->arch_opts : "",
                          target ? who->arch_target ? who->arch_target : ""
                                 : who->arch_source ? who->arch_source : "",
                          who->shmem_opts ? who->shmem_opts : "",
                          who->extra_opts ? who->extra_opts : "",
                          who->hide_stderr ? who->hide_stderr : "");
    who->qs = qtest_init(cmd);
    qtest_qmp_set_event_callback(who->qs,
                                 target ? migrate_watch_for_resume
                                        : migrate_watch_for_stop,
                                 &who->got_event);
}

void guest_use_dirty_ring(GuestState *vm)
{
    g_assert(vm->kvm_opts == NULL);
    vm->kvm_opts = g_strdup(",dirty-ring-size=4096");
}

/*
 * Wait for some output in the serial output file,
 * we get an 'A' followed by an endless string of 'B's
 * but on the destination we won't have the A.
 */
void wait_for_serial(GuestState *vm)
{
    FILE *serialfile = fopen(vm->serial_path, "r");
    const char *arch = qtest_get_arch();
    /* see serial_path comment on GuestState definition */
    int started = (strstr(vm->serial_path, "target") == NULL &&
                   strcmp(arch, "ppc64") == 0) ? 0 : 1;

    do {
        int readvalue = fgetc(serialfile);

        if (!started) {
            /* SLOF prints its banner before starting test,
             * to ignore it, mark the start of the test with '_',
             * ignore all characters until this marker
             */
            switch (readvalue) {
            case '_':
                started = 1;
                break;
            case EOF:
                fseek(serialfile, 0, SEEK_SET);
                usleep(1000);
                break;
            }
            continue;
        }
        switch (readvalue) {
        case 'A':
            /* Fine */
            break;

        case 'B':
            /* It's alive! */
            fclose(serialfile);
            return;

        case EOF:
            started = (strstr(vm->serial_path, "target") == NULL &&
                       strcmp(arch, "ppc64") == 0) ? 0 : 1;
            fseek(serialfile, 0, SEEK_SET);
            usleep(1000);
            break;

        default:
            fprintf(stderr, "Unexpected %d on %s serial\n", readvalue,
                    vm->serial_path);
            g_assert_not_reached();
        }
    } while (true);
}

bool kvm_dirty_ring_supported(void)
{
#if defined(__linux__) && defined(HOST_X86_64)
    int ret, kvm_fd = open("/dev/kvm", O_RDONLY);

    if (kvm_fd < 0) {
        return false;
    }

    ret = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_DIRTY_LOG_RING);
    close(kvm_fd);

    /* We test with 4096 slots */
    if (ret < 4096) {
        return false;
    }

    return true;
#else
    return false;
#endif
}
