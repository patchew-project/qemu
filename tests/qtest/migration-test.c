/*
 * QTest testcase for migration
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

#include "libqtest.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "crypto/tlscredspsk.h"
#include "qapi/qmp/qlist.h"

#include "migration-helpers.h"
#include "tests/migration/migration-test.h"
#ifdef CONFIG_GNUTLS
# include "tests/unit/crypto-tls-psk-helpers.h"
# ifdef CONFIG_TASN1
#  include "tests/unit/crypto-tls-x509-helpers.h"
# endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

/* For dirty ring test; so far only x86_64 is supported */
#if defined(__linux__) && defined(HOST_X86_64)
#include "linux/kvm.h"
#endif

static bool uffd_feature_thread_id;

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */

#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/vfs.h>
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include "qemu/userfaultfd.h"

static bool ufd_version_check(void)
{
    struct uffdio_api api_struct;
    uint64_t ioctl_mask;

    int ufd = uffd_open(O_CLOEXEC);

    if (ufd == -1) {
        g_test_message("Skipping test: userfaultfd not available");
        return false;
    }

    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        g_test_message("Skipping test: UFFDIO_API failed");
        return false;
    }
    uffd_feature_thread_id = api_struct.features & UFFD_FEATURE_THREAD_ID;

    ioctl_mask = (__u64)1 << _UFFDIO_REGISTER |
                 (__u64)1 << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        g_test_message("Skipping test: Missing userfault feature");
        return false;
    }

    return true;
}

#else
static bool ufd_version_check(void)
{
    g_test_message("Skipping test: Userfault not available (builtdtime)");
    return false;
}

#endif

static char *tmpfs;
static char *bootpath;

/* The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "tests/migration/i386/a-b-bootblock.h"
#include "tests/migration/aarch64/a-b-kernel.h"
#include "tests/migration/s390x/a-b-bios.h"

static void bootfile_create(char *dir)
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

static void bootfile_delete(void)
{
    unlink(bootpath);
    g_free(bootpath);
    bootpath = NULL;
}

typedef struct {
    QTestState *qs;
    /* options for source and target */
    gchar *arch_opts;
    gchar *arch_source;
    gchar *arch_target;
    const gchar *extra_opts;
    const gchar *hide_stderr;
    gchar *kvm_opts;
    const gchar *memory_size;
    /*
     * name must *not* contain "target" if it is the target of a
     * migration.
     */
    const gchar *name;
    gchar *serial_path;
    gchar *shmem_opts;
    gchar *shmem_path;
    gchar *unix_socket;
    gchar *uri;
    unsigned start_address;
    unsigned end_address;
    bool got_event;
} GuestState;

static GuestState *guest_create(const char *name)
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

static void guest_destroy(GuestState *vm)
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

static void guest_use_dirty_ring(GuestState *vm)
{
    g_assert(vm->kvm_opts == NULL);
    vm->kvm_opts = g_strdup(",dirty-ring-size=4096");
}

static void guest_use_shmem(GuestState *vm)
{
    g_assert(vm->shmem_opts == NULL);
    g_assert(vm->shmem_path == NULL);

    vm->shmem_path = g_strdup_printf("/dev/shm/qemu-%d", getpid());
    vm->shmem_opts = g_strdup_printf(
        "-object memory-backend-file,id=mem0,size=%s"
        ",mem-path=%s,share=on -numa node,memdev=mem0",
        vm->memory_size, vm->shmem_path);
}

static void guest_hide_stderr(GuestState *vm)
{
    g_assert(vm->hide_stderr == NULL);

     if (!getenv("QTEST_LOG")) {
#ifndef _WIN32
        vm->hide_stderr = "2>/dev/null";
#else
        /*
         * On Windows the QEMU executable is created via CreateProcess() and
         * IO redirection does not work, so don't bother adding IO redirection
         * to the command line.
         */
#endif
    }
}

static void guest_extra_opts(GuestState *vm, const gchar *opts)
{
    g_assert(vm->extra_opts == NULL);
    vm->extra_opts = opts;
}

static void guest_listen_unix_socket(GuestState *vm)
{
    if (vm->unix_socket) {
        unlink(vm->unix_socket);
        g_free(vm->unix_socket);
    }
    g_free(vm->uri);
    vm->unix_socket = g_strdup_printf("%s/migsocket", tmpfs);
    vm->uri = g_strdup_printf("unix:%s", vm->unix_socket);
}

static void guest_set_uri(GuestState *vm, const gchar *uri)
{
    g_free(vm->uri);
    vm->uri = g_strdup(uri);
}

/*
 * Wait for some output in the serial output file,
 * we get an 'A' followed by an endless string of 'B's
 * but on the destination we won't have the A.
 */
static void wait_for_serial(GuestState *vm)
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

/*
 * It's tricky to use qemu's migration event capability with qtest,
 * events suddenly appearing confuse the qmp()/hmp() responses.
 */

static int64_t read_ram_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return, *rsp_ram;
    int64_t result;

    rsp_return = migrate_query_not_failed(who);
    if (!qdict_haskey(rsp_return, "ram")) {
        /* Still in setup */
        result = 0;
    } else {
        rsp_ram = qdict_get_qdict(rsp_return, "ram");
        result = qdict_get_try_int(rsp_ram, property, 0);
    }
    qobject_unref(rsp_return);
    return result;
}

static int64_t read_migrate_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return;
    int64_t result;

    rsp_return = migrate_query_not_failed(who);
    result = qdict_get_try_int(rsp_return, property, 0);
    qobject_unref(rsp_return);
    return result;
}

static uint64_t get_migration_pass(QTestState *who)
{
    return read_ram_property_int(who, "dirty-sync-count");
}

static void read_blocktime(QTestState *who)
{
    QDict *rsp_return;

    rsp_return = migrate_query_not_failed(who);
    g_assert(qdict_haskey(rsp_return, "postcopy-blocktime"));
    qobject_unref(rsp_return);
}

static void wait_for_migration_pass(GuestState *who)
{
    uint64_t initial_pass = get_migration_pass(who->qs);
    uint64_t pass;

    /* Wait for the 1st sync */
    while (!who->got_event && !initial_pass) {
        usleep(1000);
        initial_pass = get_migration_pass(who->qs);
    }

    do {
        usleep(1000);
        pass = get_migration_pass(who->qs);
    } while (pass == initial_pass && !who->got_event);
}

static void check_guests_ram(GuestState *who)
{
    /* Our ASM test will have been incrementing one byte from each page from
     * start_address to < end_address in order. This gives us a constraint
     * that any page's byte should be equal or less than the previous pages
     * byte (mod 256); and they should all be equal except for one transition
     * at the point where we meet the incrementer. (We're running this with
     * the guest stopped).
     */
    unsigned address;
    uint8_t first_byte;
    uint8_t last_byte;
    bool hit_edge = false;
    int bad = 0;

    qtest_memread(who->qs, who->start_address, &first_byte, 1);
    last_byte = first_byte;

    for (address = who->start_address + TEST_MEM_PAGE_SIZE;
         address < who->end_address;
         address += TEST_MEM_PAGE_SIZE)
    {
        uint8_t b;
        qtest_memread(who->qs, address, &b, 1);
        if (b != last_byte) {
            if (((b + 1) % 256) == last_byte && !hit_edge) {
                /* This is OK, the guest stopped at the point of
                 * incrementing the previous page but didn't get
                 * to us yet.
                 */
                hit_edge = true;
                last_byte = b;
            } else {
                bad++;
                if (bad <= 10) {
                    fprintf(stderr, "Memory content inconsistency at %x"
                            " first_byte = %x last_byte = %x current = %x"
                            " hit_edge = %x\n",
                            address, first_byte, last_byte, b, hit_edge);
                }
            }
        }
    }
    if (bad >= 10) {
        fprintf(stderr, "and in another %d pages", bad - 10);
    }
    g_assert(bad == 0);
}

static char *SocketAddress_to_str(SocketAddress *addr)
{
    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        return g_strdup_printf("tcp:%s:%s",
                               addr->u.inet.host,
                               addr->u.inet.port);
    case SOCKET_ADDRESS_TYPE_UNIX:
        return g_strdup_printf("unix:%s",
                               addr->u.q_unix.path);
    case SOCKET_ADDRESS_TYPE_FD:
        return g_strdup_printf("fd:%s", addr->u.fd.str);
    case SOCKET_ADDRESS_TYPE_VSOCK:
        return g_strdup_printf("tcp:%s:%s",
                               addr->u.vsock.cid,
                               addr->u.vsock.port);
    default:
        return g_strdup("unknown address type");
    }
}

static char *migrate_get_socket_address(QTestState *who, const char *parameter)
{
    QDict *rsp;
    char *result;
    SocketAddressList *addrs;
    Visitor *iv = NULL;
    QObject *object;

    rsp = migrate_query(who);
    object = qdict_get(rsp, parameter);

    iv = qobject_input_visitor_new(object);
    visit_type_SocketAddressList(iv, NULL, &addrs, &error_abort);
    visit_free(iv);

    /* we are only using a single address */
    result = SocketAddress_to_str(addrs->value);

    qapi_free_SocketAddressList(addrs);
    qobject_unref(rsp);
    return result;
}

static long long migrate_get_parameter_int(QTestState *who,
                                           const char *parameter)
{
    QDict *rsp;
    long long result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = qdict_get_int(rsp, parameter);
    qobject_unref(rsp);
    return result;
}

static void migrate_check_parameter_int(QTestState *who, const char *parameter,
                                        long long value)
{
    long long result;

    result = migrate_get_parameter_int(who, parameter);
    g_assert_cmpint(result, ==, value);
}

static void migrate_set_parameter_int(QTestState *who, const char *parameter,
                                      long long value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %lld } }",
                             parameter, value);
    migrate_check_parameter_int(who, parameter, value);
}

static char *migrate_get_parameter_str(QTestState *who,
                                       const char *parameter)
{
    QDict *rsp;
    char *result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = g_strdup(qdict_get_str(rsp, parameter));
    qobject_unref(rsp);
    return result;
}

static void migrate_check_parameter_str(QTestState *who, const char *parameter,
                                        const char *value)
{
    g_autofree char *result = migrate_get_parameter_str(who, parameter);
    g_assert_cmpstr(result, ==, value);
}

static void migrate_set_parameter_str(QTestState *who, const char *parameter,
                                      const char *value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %s } }",
                             parameter, value);
    migrate_check_parameter_str(who, parameter, value);
}

static long long migrate_get_parameter_bool(QTestState *who,
                                           const char *parameter)
{
    QDict *rsp;
    int result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = qdict_get_bool(rsp, parameter);
    qobject_unref(rsp);
    return !!result;
}

static void migrate_check_parameter_bool(QTestState *who, const char *parameter,
                                        int value)
{
    int result;

    result = migrate_get_parameter_bool(who, parameter);
    g_assert_cmpint(result, ==, value);
}

static void migrate_set_parameter_bool(QTestState *who, const char *parameter,
                                      int value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %i } }",
                             parameter, value);
    migrate_check_parameter_bool(who, parameter, value);
}

static void migrate_ensure_non_converge(QTestState *who)
{
    /* Can't converge with 1ms downtime + 3 mbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 3 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 1);
}

static void migrate_ensure_converge(QTestState *who)
{
    /* Should converge with 30s downtime + 1 gbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 1 * 1000 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 30 * 1000);
}

static void migrate_pause(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate-pause' }");
}

static void migrate_continue(QTestState *who, const char *state)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-continue',"
                             "  'arguments': { 'state': %s } }",
                             state);
}

static void migrate_recover(QTestState *who, const char *uri)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-recover', "
                             "  'id': 'recover-cmd', "
                             "  'arguments': { 'uri': %s } }",
                             uri);
}

static void migrate_cancel(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate_cancel' }");
}

static void migrate_set_capability(QTestState *who, const char *capability,
                                   bool value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-capabilities',"
                             "'arguments': { "
                             "'capabilities': [ { "
                             "'capability': %s, 'state': %i } ] } }",
                             capability, value);
}

static void migrate_postcopy_start(GuestState *from, GuestState *to)
{
    qtest_qmp_assert_success(from->qs,
                             "{ 'execute': 'migrate-start-postcopy' }");

    if (!from->got_event) {
        qtest_qmp_eventwait(from->qs, "STOP");
    }

    qtest_qmp_eventwait(to->qs, "RESUME");
}

static void do_migrate(GuestState *from, GuestState *to)
{
    if (!to->uri || strncmp(to->uri, "tcp:", strlen("tcp:")) == 0) {
        g_autofree char *tcp_uri =
            migrate_get_socket_address(to->qs, "socket-address");
        migrate_qmp(from->qs, tcp_uri, "{}");
    } else {
        migrate_qmp(from->qs, to->uri, "{}");
    }
}

typedef struct {
    /* only launch the target process */
    bool only_target;
} MigrateStart;

/*
 * A hook that runs after the src and dst QEMUs have been
 * created, but before the migration is started. This can
 * be used to set migration parameters and capabilities.
 *
 * Returns: NULL, or a pointer to opaque state to be
 *          later passed to the TestMigrateFinishHook
 */
typedef void * (*TestMigrateStartHook)(GuestState *from, GuestState *to);

/*
 * A hook that runs after the migration has finished,
 * regardless of whether it succeeded or failed, but
 * before QEMU has terminated (unless it self-terminated
 * due to migration error)
 *
 * @opaque is a pointer to state previously returned
 * by the TestMigrateStartHook if any, or NULL.
 */
typedef void (*TestMigrateFinishHook)(GuestState *from, GuestState *to,
                                      void *opaque);

typedef struct {
    /* Optional: fine tune start parameters */
    MigrateStart start;

    /* Optional: callback to run at start to set migration parameters */
    TestMigrateStartHook start_hook;
    /* Optional: callback to run at finish to cleanup */
    TestMigrateFinishHook finish_hook;

    /*
     * Optional: normally we expect the migration process to complete.
     *
     * There can be a variety of reasons and stages in which failure
     * can happen during tests.
     *
     * If a failure is expected to happen at time of establishing
     * the connection, then MIG_TEST_FAIL will indicate that the dst
     * QEMU is expected to stay running and accept future migration
     * connections.
     *
     * If a failure is expected to happen while processing the
     * migration stream, then MIG_TEST_FAIL_DEST_QUIT_ERR will indicate
     * that the dst QEMU is expected to quit with non-zero exit status
     */
    enum {
        /* This test should succeed, the default */
        MIG_TEST_SUCCEED = 0,
        /* This test should fail, dest qemu should keep alive */
        MIG_TEST_FAIL,
        /* This test should fail, dest qemu should fail with abnormal status */
        MIG_TEST_FAIL_DEST_QUIT_ERR,
    } result;

    /* Optional: set number of migration passes to wait for, if live==true */
    unsigned int iterations;

    /*
     * Optional: whether the guest CPUs should be running during a precopy
     * migration test.  We used to always run with live but it took much
     * longer so we reduced live tests to only the ones that have solid
     * reason to be tested live-only.  For each of the new test cases for
     * precopy please provide justifications to use live explicitly (please
     * refer to existing ones with live=true), or use live=off by default.
     */
    bool live;

    /* Postcopy specific fields */
    void *postcopy_data;
    bool postcopy_preempt;
} MigrateCommon;

static void test_migrate_start(GuestState *from, GuestState *to,
                               MigrateStart *args)
{
    g_autofree gchar *cmd_source = NULL;
    g_autofree gchar *cmd_target = NULL;

    cmd_source = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-name %s,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s "
                                 "%s %s %s %s %s",
                                 from->kvm_opts ? from->kvm_opts : "",
                                 from->name,
                                 from->memory_size,
                                 from->serial_path,
                                 from->arch_opts ? from->arch_opts : "",
                                 from->arch_source ? from->arch_source : "",
                                 from->shmem_opts ? from->shmem_opts : "",
                                 from->extra_opts ? from->extra_opts : "",
                                 from->hide_stderr ? from->hide_stderr : "");

    if (!args->only_target) {
        from->qs = qtest_init(cmd_source);
        qtest_qmp_set_event_callback(from->qs,
                                     migrate_watch_for_stop,
                                     &from->got_event);
    }

    cmd_target = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-name %s,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s "
                                 "-incoming %s "
                                 "%s %s %s %s %s",
                                 to->kvm_opts ? to->kvm_opts : "",
                                 to->name,
                                 to->memory_size,
                                 to->serial_path,
                                 to->uri ? to->uri : "defer",
                                 to->arch_opts ? to->arch_opts : "",
                                 to->arch_target ? to->arch_target : "",
                                 to->shmem_opts ? to->shmem_opts : "",
                                 to->extra_opts ? to->extra_opts : "",
                                 to->hide_stderr ? to->hide_stderr : "");
    to->qs = qtest_init(cmd_target);
    qtest_qmp_set_event_callback(to->qs,
                                 migrate_watch_for_resume,
                                 &to->got_event);
}

static void test_migrate_end(GuestState *from, GuestState *to, bool test_dest)
{
    unsigned char dest_byte_a, dest_byte_b, dest_byte_c, dest_byte_d;

    guest_destroy(from);

    if (test_dest) {
        qtest_memread(to->qs, to->start_address, &dest_byte_a, 1);

        /* Destination still running, wait for a byte to change */
        do {
            qtest_memread(to->qs, to->start_address, &dest_byte_b, 1);
            usleep(1000 * 10);
        } while (dest_byte_a == dest_byte_b);

        qtest_qmp_assert_success(to->qs, "{ 'execute' : 'stop'}");

        /* With it stopped, check nothing changes */
        qtest_memread(to->qs, to->start_address, &dest_byte_c, 1);
        usleep(1000 * 200);
        qtest_memread(to->qs, to->start_address, &dest_byte_d, 1);
        g_assert_cmpint(dest_byte_c, ==, dest_byte_d);

        check_guests_ram(to);
    }

    guest_destroy(to);
}

#ifdef CONFIG_GNUTLS
struct TestMigrateTLSPSKData {
    char *workdir;
    char *workdiralt;
    char *pskfile;
    char *pskfilealt;
};

static void *
test_migrate_tls_psk_start_common(GuestState *from, GuestState *to,
                                  bool mismatch)
{
    struct TestMigrateTLSPSKData *data =
        g_new0(struct TestMigrateTLSPSKData, 1);

    data->workdir = g_strdup_printf("%s/tlscredspsk0", tmpfs);
    data->pskfile = g_strdup_printf("%s/%s", data->workdir,
                                    QCRYPTO_TLS_CREDS_PSKFILE);
    g_mkdir_with_parents(data->workdir, 0700);
    test_tls_psk_init(data->pskfile);

    if (mismatch) {
        data->workdiralt = g_strdup_printf("%s/tlscredspskalt0", tmpfs);
        data->pskfilealt = g_strdup_printf("%s/%s", data->workdiralt,
                                           QCRYPTO_TLS_CREDS_PSKFILE);
        g_mkdir_with_parents(data->workdiralt, 0700);
        test_tls_psk_init_alt(data->pskfilealt);
    }

    qtest_qmp_assert_success(from->qs,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'username': 'qemu'} }",
                             data->workdir);

    qtest_qmp_assert_success(to->qs,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s } }",
                             mismatch ? data->workdiralt : data->workdir);

    migrate_set_parameter_str(from->qs, "tls-creds", "tlscredspsk0");
    migrate_set_parameter_str(to->qs, "tls-creds", "tlscredspsk0");

    return data;
}

static void *
test_migrate_tls_psk_start_match(GuestState *from, GuestState *to)
{
    return test_migrate_tls_psk_start_common(from, to, false);
}

static void *
test_migrate_tls_psk_start_mismatch(GuestState *from, GuestState *to)
{
    return test_migrate_tls_psk_start_common(from, to, true);
}

static void
test_migrate_tls_psk_finish(GuestState *from, GuestState *to, void *opaque)
{
    struct TestMigrateTLSPSKData *data = opaque;

    test_tls_psk_cleanup(data->pskfile);
    if (data->pskfilealt) {
        test_tls_psk_cleanup(data->pskfilealt);
    }
    rmdir(data->workdir);
    if (data->workdiralt) {
        rmdir(data->workdiralt);
    }

    g_free(data->workdiralt);
    g_free(data->pskfilealt);
    g_free(data->workdir);
    g_free(data->pskfile);
    g_free(data);
}

#ifdef CONFIG_TASN1
typedef struct {
    char *workdir;
    char *keyfile;
    char *cacert;
    char *servercert;
    char *serverkey;
    char *clientcert;
    char *clientkey;
} TestMigrateTLSX509Data;

typedef struct {
    bool verifyclient;
    bool clientcert;
    bool hostileclient;
    bool authzclient;
    const char *certhostname;
    const char *certipaddr;
} TestMigrateTLSX509;

static void *
test_migrate_tls_x509_start_common(GuestState *from, GuestState *to,
                                   TestMigrateTLSX509 *args)
{
    TestMigrateTLSX509Data *data = g_new0(TestMigrateTLSX509Data, 1);

    data->workdir = g_strdup_printf("%s/tlscredsx5090", tmpfs);
    data->keyfile = g_strdup_printf("%s/key.pem", data->workdir);

    data->cacert = g_strdup_printf("%s/ca-cert.pem", data->workdir);
    data->serverkey = g_strdup_printf("%s/server-key.pem", data->workdir);
    data->servercert = g_strdup_printf("%s/server-cert.pem", data->workdir);
    if (args->clientcert) {
        data->clientkey = g_strdup_printf("%s/client-key.pem", data->workdir);
        data->clientcert = g_strdup_printf("%s/client-cert.pem", data->workdir);
    }

    g_mkdir_with_parents(data->workdir, 0700);

    test_tls_init(data->keyfile);
#ifndef _WIN32
    g_assert(link(data->keyfile, data->serverkey) == 0);
#else
    g_assert(CreateHardLink(data->serverkey, data->keyfile, NULL) != 0);
#endif
    if (args->clientcert) {
#ifndef _WIN32
        g_assert(link(data->keyfile, data->clientkey) == 0);
#else
        g_assert(CreateHardLink(data->clientkey, data->keyfile, NULL) != 0);
#endif
    }

    TLS_ROOT_REQ_SIMPLE(cacertreq, data->cacert);
    if (args->clientcert) {
        TLS_CERT_REQ_SIMPLE_CLIENT(servercertreq, cacertreq,
                                   args->hostileclient ?
                                   QCRYPTO_TLS_TEST_CLIENT_HOSTILE_NAME :
                                   QCRYPTO_TLS_TEST_CLIENT_NAME,
                                   data->clientcert);
    }

    TLS_CERT_REQ_SIMPLE_SERVER(clientcertreq, cacertreq,
                               data->servercert,
                               args->certhostname,
                               args->certipaddr);

    qtest_qmp_assert_success(from->qs,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509client0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': true} }",
                             data->workdir);
    migrate_set_parameter_str(from->qs, "tls-creds", "tlscredsx509client0");
    if (args->certhostname) {
        migrate_set_parameter_str(from->qs, "tls-hostname", args->certhostname);
    }

    qtest_qmp_assert_success(to->qs,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509server0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': %i} }",
                             data->workdir, args->verifyclient);
    migrate_set_parameter_str(to->qs, "tls-creds", "tlscredsx509server0");

    if (args->authzclient) {
        qtest_qmp_assert_success(to->qs,
                                 "{ 'execute': 'object-add',"
                                 "  'arguments': { 'qom-type': 'authz-simple',"
                                 "                 'id': 'tlsauthz0',"
                                 "                 'identity': %s} }",
                                 "CN=" QCRYPTO_TLS_TEST_CLIENT_NAME);
        migrate_set_parameter_str(to->qs, "tls-authz", "tlsauthz0");
    }

    return data;
}

/*
 * The normal case: match server's cert hostname against
 * whatever host we were telling QEMU to connect to (if any)
 */
static void *
test_migrate_tls_x509_start_default_host(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "127.0.0.1"
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to (if any),
 * so we must give QEMU an explicit hostname to validate
 */
static void *
test_migrate_tls_x509_start_override_host(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certhostname = "qemu.org",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to, and so we
 * expect the client to reject the server
 */
static void *
test_migrate_tls_x509_start_mismatch_host(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "10.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void *
test_migrate_tls_x509_start_friendly_client(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void *
test_migrate_tls_x509_start_hostile_client(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .hostileclient = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and no server verification
 */
static void *
test_migrate_tls_x509_start_allow_anon_client(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and server verification rejecting
 */
static void *
test_migrate_tls_x509_start_reject_anon_client(GuestState *from, GuestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void
test_migrate_tls_x509_finish(GuestState *from, GuestState *to, void *opaque)
{
    TestMigrateTLSX509Data *data = opaque;

    test_tls_cleanup(data->keyfile);
    g_free(data->keyfile);

    unlink(data->cacert);
    g_free(data->cacert);
    unlink(data->servercert);
    g_free(data->servercert);
    unlink(data->serverkey);
    g_free(data->serverkey);

    if (data->clientcert) {
        unlink(data->clientcert);
        g_free(data->clientcert);
    }
    if (data->clientkey) {
        unlink(data->clientkey);
        g_free(data->clientkey);
    }

    rmdir(data->workdir);
    g_free(data->workdir);

    g_free(data);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

static void *
test_migrate_compress_start(GuestState *from, GuestState *to)
{
    migrate_set_parameter_int(from->qs, "compress-level", 1);
    migrate_set_parameter_int(from->qs, "compress-threads", 4);
    migrate_set_parameter_bool(from->qs, "compress-wait-thread", true);
    migrate_set_parameter_int(to->qs, "decompress-threads", 4);

    migrate_set_capability(from->qs, "compress", true);
    migrate_set_capability(to->qs, "compress", true);

    return NULL;
}

static void *
test_migrate_compress_nowait_start(GuestState *from, GuestState *to)
{
    migrate_set_parameter_int(from->qs, "compress-level", 9);
    migrate_set_parameter_int(from->qs, "compress-threads", 1);
    migrate_set_parameter_bool(from->qs, "compress-wait-thread", false);
    migrate_set_parameter_int(to->qs, "decompress-threads", 1);

    migrate_set_capability(from->qs, "compress", true);
    migrate_set_capability(to->qs, "compress", true);

    return NULL;
}

static void migrate_postcopy_prepare(GuestState *from,
                                     GuestState *to,
                                     MigrateCommon *args)
{
    guest_listen_unix_socket(to);
    test_migrate_start(from, to, &args->start);

    if (args->start_hook) {
        args->postcopy_data = args->start_hook(from, to);
    }

    migrate_set_capability(from->qs, "postcopy-ram", true);
    migrate_set_capability(to->qs, "postcopy-ram", true);
    migrate_set_capability(to->qs, "postcopy-blocktime", true);

    if (args->postcopy_preempt) {
        migrate_set_capability(from->qs, "postcopy-preempt", true);
        migrate_set_capability(to->qs, "postcopy-preempt", true);
    }

    migrate_ensure_non_converge(from->qs);

    /* Wait for the first serial output from the source */
    wait_for_serial(from);

    do_migrate(from, to);

    wait_for_migration_pass(from);
}

static void migrate_postcopy_complete(GuestState *from, GuestState *to,
                                      MigrateCommon *args)
{
    wait_for_migration_complete(from->qs);

    /* Make sure we get at least one "B" on destination */
    wait_for_serial(to);

    if (uffd_feature_thread_id) {
        read_blocktime(to->qs);
    }

    if (args->finish_hook) {
        args->finish_hook(from, to, args->postcopy_data);
        args->postcopy_data = NULL;
    }

    test_migrate_end(from, to, true);
}

static void test_postcopy_common(GuestState *from, GuestState *to,
                                 MigrateCommon *args)
{
    migrate_postcopy_prepare(from, to, args);
    migrate_postcopy_start(from, to);
    migrate_postcopy_complete(from, to, args);
}

static void test_postcopy(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = { };

    test_postcopy_common(from, to, &args);
}

static void test_postcopy_compress(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_compress_start
    };

    test_postcopy_common(from, to, &args);
}

static void test_postcopy_preempt(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_common(from, to, &args);
}

#ifdef CONFIG_GNUTLS
static void test_postcopy_tls_psk(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_common(from, to, &args);
}

static void test_postcopy_preempt_tls_psk(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_common(from, to, &args);
}
#endif

static void test_postcopy_recovery_common(MigrateCommon *args)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");

    /* Always hide errors for postcopy recover tests since they're expected */
    guest_hide_stderr(from);
    guest_hide_stderr(to);
    migrate_postcopy_prepare(from, to, args);

    /* Turn postcopy speed down, 4K/s is slow enough on any machines */
    migrate_set_parameter_int(from->qs, "max-postcopy-bandwidth", 4096);

    /* Now we start the postcopy */
    migrate_postcopy_start(from, to);

    /*
     * Wait until postcopy is really started; we can only run the
     * migrate-pause command during a postcopy
     */
    wait_for_migration_status(from->qs, "postcopy-active", NULL);

    /*
     * Manually stop the postcopy migration. This emulates a network
     * failure with the migration socket
     */
    migrate_pause(from->qs);

    /*
     * Wait for destination side to reach postcopy-paused state.  The
     * migrate-recover command can only succeed if destination machine
     * is in the paused state
     */
    wait_for_migration_status(to->qs, "postcopy-paused",
                              (const char * []) { "failed", "active",
                                                  "completed", NULL });

    /*
     * Create a new socket to emulate a new channel that is different
     * from the broken migration channel; tell the destination to
     * listen to the new port
     */
    guest_listen_unix_socket(to);
    migrate_recover(to->qs, to->uri);

    /*
     * Try to rebuild the migration channel using the resume flag and
     * the newly created channel
     */
    wait_for_migration_status(from->qs, "postcopy-paused",
                              (const char * []) { "failed", "active",
                                                  "completed", NULL });
    migrate_qmp(from->qs, to->uri, "{'resume': true}");

    /* Restore the postcopy bandwidth to unlimited */
    migrate_set_parameter_int(from->qs, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to, args);
}

static void test_postcopy_recovery(void)
{
    MigrateCommon args = { };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_compress(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_compress_start
    };

    test_postcopy_recovery_common(&args);
}

#ifdef CONFIG_GNUTLS
static void test_postcopy_recovery_tls_psk(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_recovery_common(&args);
}
#endif

static void test_postcopy_preempt_recovery(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_recovery_common(&args);
}

#ifdef CONFIG_GNUTLS
/* This contains preempt+recovery+tls test altogether */
static void test_postcopy_preempt_all(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_recovery_common(&args);
}

#endif

static void test_baddest(void)
{
    MigrateStart args = { };
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_migrate_start(from, to, &args);
    /*
     * Don't change to do_migrate(). We are using a wrong uri on purpose.
     */
    migrate_qmp(from->qs, "tcp:127.0.0.1:0", "{}");
    wait_for_migration_fail(from->qs, false);
    test_migrate_end(from, to, false);
}

static void test_precopy_common(GuestState *from, GuestState *to,
                                MigrateCommon *args)
{
    void *data_hook = NULL;

    test_migrate_start(from, to, &args->start);

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    /* Wait for the first serial output from the source */
    if (args->result == MIG_TEST_SUCCEED) {
        wait_for_serial(from);
    }

    if (args->live) {
        /*
         * Testing live migration, we want to ensure that some
         * memory is re-dirtied after being transferred, so that
         * we exercise logic for dirty page handling. We achieve
         * this with a ridiculosly low bandwidth that guarantees
         * non-convergance.
         */
        migrate_ensure_non_converge(from->qs);
    } else {
        /*
         * Testing non-live migration, we allow it to run at
         * full speed to ensure short test case duration.
         * For tests expected to fail, we don't need to
         * change anything.
         */
        if (args->result == MIG_TEST_SUCCEED) {
            qtest_qmp_assert_success(from->qs, "{ 'execute' : 'stop'}");
            if (!from->got_event) {
                qtest_qmp_eventwait(from->qs, "STOP");
            }
            migrate_ensure_converge(from->qs);
        }
    }

    do_migrate(from, to);

    if (args->result != MIG_TEST_SUCCEED) {
        bool allow_active = args->result == MIG_TEST_FAIL;
        wait_for_migration_fail(from->qs, allow_active);

        if (args->result == MIG_TEST_FAIL_DEST_QUIT_ERR) {
            qtest_set_expected_status(to->qs, EXIT_FAILURE);
        }
    } else {
        if (args->live) {
            if (args->iterations) {
                while (args->iterations--) {
                    wait_for_migration_pass(from);
                }
            } else {
                wait_for_migration_pass(from);
            }

            migrate_ensure_converge(from->qs);

            /*
             * We do this first, as it has a timeout to stop us
             * hanging forever if migration didn't converge
             */
            wait_for_migration_complete(from->qs);

            if (!from->got_event) {
                qtest_qmp_eventwait(from->qs, "STOP");
            }
        } else {
            wait_for_migration_complete(from->qs);
            /*
             * Must wait for dst to finish reading all incoming
             * data on the socket before issuing 'cont' otherwise
             * it'll be ignored
             */
            wait_for_migration_complete(to->qs);

            qtest_qmp_assert_success(to->qs, "{ 'execute' : 'cont'}");
        }

        if (!to->got_event) {
            qtest_qmp_eventwait(to->qs, "RESUME");
        }

        wait_for_serial(to);
    }

    if (args->finish_hook) {
        args->finish_hook(from, to, data_hook);
    }

    test_migrate_end(from, to, args->result == MIG_TEST_SUCCEED);
}

static void test_precopy_unix_plain(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        /*
         * The simplest use case of precopy, covering smoke tests of
         * get-dirty-log dirty tracking.
         */
        .live = true,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}


static void test_precopy_unix_dirty_ring(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        /*
         * Besides the precopy/unix basic test, cover dirty ring interface
         * rather than get-dirty-log.
         */
        .live = true,
    };

    guest_use_dirty_ring(from);
    guest_use_dirty_ring(to);
    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_GNUTLS
static void test_precopy_unix_tls_psk(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_TASN1
static void test_precopy_unix_tls_x509_default_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

static void test_precopy_unix_tls_x509_override_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

static void test_ignore_shared(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateStart args = { };

    guest_use_shmem(from);
    guest_use_shmem(to);
    guest_listen_unix_socket(to);
    test_migrate_start(from, to, &args);

    migrate_set_capability(from->qs, "x-ignore-shared", true);
    migrate_set_capability(to->qs, "x-ignore-shared", true);

    /* Wait for the first serial output from the source */
    wait_for_serial(from);

    do_migrate(from, to);

    wait_for_migration_pass(from);

    if (!from->got_event) {
        qtest_qmp_eventwait(from->qs, "STOP");
    }

    qtest_qmp_eventwait(to->qs, "RESUME");

    wait_for_serial(to);
    wait_for_migration_complete(from->qs);

    /* Check whether shared RAM has been really skipped */
    g_assert_cmpint(
        read_ram_property_int(from->qs, "transferred"), <, 1024 * 1024);

    test_migrate_end(from, to, true);
}

static void *
test_migrate_xbzrle_start(GuestState *from, GuestState *to)
{
    migrate_set_parameter_int(from->qs, "xbzrle-cache-size", 33554432);

    migrate_set_capability(from->qs, "xbzrle", true);
    migrate_set_capability(to->qs, "xbzrle", true);

    return NULL;
}

static void test_precopy_unix_xbzrle(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_xbzrle_start,
        .iterations = 2,
        /*
         * XBZRLE needs pages to be modified when doing the 2nd+ round
         * iteration to have real data pushed to the stream.
         */
        .live = true,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

static void test_precopy_unix_compress(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_compress_start,
        /*
         * Test that no invalid thread state is left over from
         * the previous iteration.
         */
        .iterations = 2,
        /*
         * We make sure the compressor can always work well even if guest
         * memory is changing.  See commit 34ab9e9743 where we used to fix
         * a bug when only trigger-able with guest memory changing.
         */
        .live = true,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

static void test_precopy_unix_compress_nowait(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_compress_nowait_start,
        /*
         * Test that no invalid thread state is left over from
         * the previous iteration.
         */
        .iterations = 2,
        /* Same reason for the wait version of precopy compress test */
        .live = true,
    };

    guest_listen_unix_socket(to);
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_plain(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = { };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_GNUTLS
static void test_precopy_tcp_tls_psk_match(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_psk_mismatch(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_mismatch,
        .finish_hook = test_migrate_tls_psk_finish,
        .result = MIG_TEST_FAIL,
    };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_TASN1
static void test_precopy_tcp_tls_x509_default_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_override_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_mismatch_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_mismatch_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_friendly_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_friendly_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_hostile_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_hostile_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_allow_anon_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_allow_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}

static void test_precopy_tcp_tls_x509_reject_anon_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_tls_x509_start_reject_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_set_uri(to, "tcp:127.0.0.1:0");
    test_precopy_common(from, to, &args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

#ifndef _WIN32
static void *test_migrate_fd_start_hook(GuestState *from, GuestState *to)
{
    int ret;
    int pair[2];

    /* Create two connected sockets for migration */
    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair);
    g_assert_cmpint(ret, ==, 0);

    /* Send the 1st socket to the target */
    qtest_qmp_fds_assert_success(to->qs, &pair[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[0]);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to->qs, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'fd:fd-mig' }}");
    guest_set_uri(to, "fd:fd-mig");

    /* Send the 2nd socket to the target */
    qtest_qmp_fds_assert_success(from->qs, &pair[1], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[1]);

    return NULL;
}

static void test_migrate_fd_finish_hook(GuestState *from, GuestState *to,
                                        void *opaque)
{
    QDict *rsp;
    const char *error_desc;

    /* Test closing fds */
    /* We assume, that QEMU removes named fd from its list,
     * so this should fail */
    rsp = qtest_qmp(from->qs, "{ 'execute': 'closefd',"
                          "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);

    rsp = qtest_qmp(to->qs, "{ 'execute': 'closefd',"
                        "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);
}

static void test_migrate_fd_proto(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_fd_start_hook,
        .finish_hook = test_migrate_fd_finish_hook
    };
    test_precopy_common(from, to, &args);
}
#endif /* _WIN32 */

static void do_test_validate_uuid(GuestState *from, GuestState *to,
                                  MigrateStart *args, bool should_fail)
{
    guest_listen_unix_socket(to);
    test_migrate_start(from, to, args);

    /*
     * UUID validation is at the begin of migration. So, the main process of
     * migration is not interesting for us here. Thus, set huge downtime for
     * very fast migration.
     */
    migrate_set_parameter_int(from->qs, "downtime-limit", 1000000);
    migrate_set_capability(from->qs, "validate-uuid", true);

    /* Wait for the first serial output from the source */
    wait_for_serial(from);

    do_migrate(from, to);

    if (should_fail) {
        qtest_set_expected_status(to->qs, EXIT_FAILURE);
        wait_for_migration_fail(from->qs, true);
    } else {
        wait_for_migration_complete(from->qs);
    }

    test_migrate_end(from, to, false);
}

static void test_validate_uuid(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateStart args = { };

    guest_extra_opts(from, "-uuid 11111111-1111-1111-1111-111111111111");
    guest_extra_opts(to, "-uuid 11111111-1111-1111-1111-111111111111");
    do_test_validate_uuid(from, to, &args, false);
}

static void test_validate_uuid_error(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateStart args = { };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_extra_opts(from, "-uuid 11111111-1111-1111-1111-111111111111");
    guest_extra_opts(to, "-uuid 22222222-2222-2222-2222-222222222222");
    do_test_validate_uuid(from, to, &args, true);
}

static void test_validate_uuid_src_not_set(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateStart args = { };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_extra_opts(to, "-uuid 22222222-2222-2222-2222-222222222222");
    do_test_validate_uuid(from, to, &args, false);
}

static void test_validate_uuid_dst_not_set(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateStart args = { };

    guest_hide_stderr(from);
    guest_hide_stderr(to);
    guest_extra_opts(from, "-uuid 11111111-1111-1111-1111-111111111111");
    do_test_validate_uuid(from, to, &args, false);
}

/*
 * The way auto_converge works, we need to do too many passes to
 * run this test.  Auto_converge logic is only run once every
 * three iterations, so:
 *
 * - 3 iterations without auto_converge enabled
 * - 3 iterations with pct = 5
 * - 3 iterations with pct = 30
 * - 3 iterations with pct = 55
 * - 3 iterations with pct = 80
 * - 3 iterations with pct = 95 (max(95, 80 + 25))
 *
 * To make things even worse, we need to run the initial stage at
 * 3MB/s so we enter autoconverge even when host is (over)loaded.
 */
static void test_migrate_auto_converge(void)
{
    MigrateStart args = {};
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    int64_t percentage;

    /*
     * We want the test to be stable and as fast as possible.
     * E.g., with 1Gb/s bandwith migration may pass without throttling,
     * so we need to decrease a bandwidth.
     */
    const int64_t init_pct = 5, inc_pct = 25, max_pct = 95;

    guest_listen_unix_socket(to);
    test_migrate_start(from, to, &args);

    migrate_set_capability(from->qs, "auto-converge", true);
    migrate_set_parameter_int(from->qs, "cpu-throttle-initial", init_pct);
    migrate_set_parameter_int(from->qs, "cpu-throttle-increment", inc_pct);
    migrate_set_parameter_int(from->qs, "max-cpu-throttle", max_pct);

    /*
     * Set the initial parameters so that the migration could not converge
     * without throttling.
     */
    migrate_ensure_non_converge(from->qs);

    /* To check remaining size after precopy */
    migrate_set_capability(from->qs, "pause-before-switchover", true);

    /* Wait for the first serial output from the source */
    wait_for_serial(from);

    do_migrate(from, to);

    /* Wait for throttling begins */
    percentage = 0;
    do {
        percentage = read_migrate_property_int(from->qs,
                                               "cpu-throttle-percentage");
        if (percentage != 0) {
            break;
        }
        usleep(20);
        g_assert_false(from->got_event);
    } while (true);
    /* The first percentage of throttling should be at least init_pct */
    g_assert_cmpint(percentage, >=, init_pct);
    /* Now, when we tested that throttling works, let it converge */
    migrate_ensure_converge(from->qs);

    /*
     * Wait for pre-switchover status to check last throttle percentage
     * and remaining. These values will be zeroed later
     */
    wait_for_migration_status(from->qs, "pre-switchover", NULL);

    /* The final percentage of throttling shouldn't be greater than max_pct */
    percentage = read_migrate_property_int(from->qs, "cpu-throttle-percentage");
    g_assert_cmpint(percentage, <=, max_pct);
    migrate_continue(from->qs, "pre-switchover");

    qtest_qmp_eventwait(to->qs, "RESUME");

    wait_for_serial(to);
    wait_for_migration_complete(from->qs);

    test_migrate_end(from, to, true);
}

static void *
test_migrate_precopy_tcp_multifd_start_common(GuestState *from, GuestState *to,
                                              const char *method)
{
    migrate_set_parameter_int(from->qs, "multifd-channels", 16);
    migrate_set_parameter_int(to->qs, "multifd-channels", 16);

    migrate_set_parameter_str(from->qs, "multifd-compression", method);
    migrate_set_parameter_str(to->qs, "multifd-compression", method);

    migrate_set_capability(from->qs, "multifd", true);
    migrate_set_capability(to->qs, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to->qs, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    guest_set_uri(to, "tcp:127.0.0.1:0");

    return NULL;
}

static void *
test_migrate_precopy_tcp_multifd_start(GuestState *from, GuestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
}

static void *
test_migrate_precopy_tcp_multifd_zlib_start(GuestState *from, GuestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zlib");
}

#ifdef CONFIG_ZSTD
static void *
test_migrate_precopy_tcp_multifd_zstd_start(GuestState *from, GuestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zstd");
}
#endif /* CONFIG_ZSTD */

static void test_multifd_tcp_none(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_precopy_tcp_multifd_start,
        /*
         * Multifd is more complicated than most of the features, it
         * directly takes guest page buffers when sending, make sure
         * everything will work alright even if guest page is changing.
         */
        .live = true,
    };
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_zlib(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_precopy_tcp_multifd_zlib_start,
    };
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_ZSTD
static void test_multifd_tcp_zstd(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_precopy_tcp_multifd_zstd_start,
    };
    test_precopy_common(from, to, &args);
}
#endif

#ifdef CONFIG_GNUTLS
static void *
test_migrate_multifd_tcp_tls_psk_start_match(GuestState *from, GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_psk_start_match(from, to);
}

static void *
test_migrate_multifd_tcp_tls_psk_start_mismatch(GuestState *from,
                                                GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_psk_start_mismatch(from, to);
}

#ifdef CONFIG_TASN1
static void *
test_migrate_multifd_tls_x509_start_default_host(GuestState *from,
                                                 GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_default_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_override_host(GuestState *from,
                                                  GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_override_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_mismatch_host(GuestState *from,
                                                  GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_mismatch_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_allow_anon_client(GuestState *from,
                                                      GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_allow_anon_client(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_reject_anon_client(GuestState *from,
                                                       GuestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_reject_anon_client(from, to);
}
#endif /* CONFIG_TASN1 */

static void test_multifd_tcp_tls_psk_match(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tcp_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_tls_psk_mismatch(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tcp_tls_psk_start_mismatch,
        .finish_hook = test_migrate_tls_psk_finish,
        .result = MIG_TEST_FAIL,
    };
    guest_hide_stderr(from);
    guest_hide_stderr(to);
    test_precopy_common(from, to, &args);
}

#ifdef CONFIG_TASN1
static void test_multifd_tcp_tls_x509_default_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_tls_x509_override_host(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_tls_x509_mismatch_host(void)
{
    /*
     * This has different behaviour to the non-multifd case.
     *
     * In non-multifd case when client aborts due to mismatched
     * cert host, the server has already started trying to load
     * migration state, and so it exits with I/O failure.
     *
     * In multifd case when client aborts due to mismatched
     * cert host, the server is still waiting for the other
     * multifd connections to arrive so hasn't started trying
     * to load migration state, and thus just aborts the migration
     * without exiting.
     */
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tls_x509_start_mismatch_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };
    guest_hide_stderr(from);
    guest_hide_stderr(to);
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_tls_x509_allow_anon_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tls_x509_start_allow_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(from, to, &args);
}

static void test_multifd_tcp_tls_x509_reject_anon_client(void)
{
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    MigrateCommon args = {
        .start_hook = test_migrate_multifd_tls_x509_start_reject_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };
    guest_hide_stderr(from);
    guest_hide_stderr(to);
    test_precopy_common(from, to, &args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

/*
 * This test does:
 *  source               target
 *                       migrate_incoming
 *     migrate
 *     migrate_cancel
 *                       launch another target
 *     migrate
 *
 *  And see that it works
 */
static void test_multifd_tcp_cancel(void)
{
    MigrateStart args = { };
    GuestState *from = guest_create("source");
    GuestState *to = guest_create("target");
    GuestState *to2 = guest_create("target2");

    guest_hide_stderr(from);
    guest_hide_stderr(to);

    test_migrate_start(from, to, &args);

    migrate_ensure_non_converge(from->qs);

    migrate_set_parameter_int(from->qs, "multifd-channels", 16);
    migrate_set_parameter_int(to->qs, "multifd-channels", 16);

    migrate_set_capability(from->qs, "multifd", true);
    migrate_set_capability(to->qs, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to->qs, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    guest_set_uri(to, "tcp:127.0.0.1:0");

    /* Wait for the first serial output from the source */
    wait_for_serial(from);

    do_migrate(from, to);

    wait_for_migration_pass(from);

    migrate_cancel(from->qs);

    /* Make sure QEMU process "to" exited */
    qtest_set_expected_status(to->qs, EXIT_FAILURE);
    qtest_wait_qemu(to->qs);

    guest_destroy(to);

    args = (MigrateStart){
        .only_target = true,
    };

    test_migrate_start(from, to2, &args);

    migrate_set_parameter_int(to2->qs, "multifd-channels", 16);

    migrate_set_capability(to2->qs, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to2->qs, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    guest_set_uri(to2, "tcp:127.0.0.1:0");

    wait_for_migration_status(from->qs, "cancelled", NULL);

    migrate_ensure_converge(from->qs);

    do_migrate(from, to2);

    wait_for_migration_pass(from);

    if (!from->got_event) {
        qtest_qmp_eventwait(from->qs, "STOP");
    }
    qtest_qmp_eventwait(to2->qs, "RESUME");

    wait_for_serial(to2);
    wait_for_migration_complete(from->qs);
    test_migrate_end(from, to2, true);
}

static void calc_dirty_rate(QTestState *who, uint64_t calc_time)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'calc-dirty-rate',"
                             "'arguments': { "
                             "'calc-time': %" PRIu64 ","
                             "'mode': 'dirty-ring' }}",
                             calc_time);
}

static QDict *query_dirty_rate(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who,
                                        "{ 'execute': 'query-dirty-rate' }");
}

static void dirtylimit_set_all(QTestState *who, uint64_t dirtyrate)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'set-vcpu-dirty-limit',"
                             "'arguments': { "
                             "'dirty-rate': %" PRIu64 " } }",
                             dirtyrate);
}

static void cancel_vcpu_dirty_limit(QTestState *who)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'cancel-vcpu-dirty-limit' }");
}

static QDict *query_vcpu_dirty_limit(QTestState *who)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'query-vcpu-dirty-limit' }");
    g_assert(!qdict_haskey(rsp, "error"));
    g_assert(qdict_haskey(rsp, "return"));

    return rsp;
}

static bool calc_dirtyrate_ready(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);

    return g_strcmp0(status, "measuring");
}

static void wait_for_calc_dirtyrate_complete(QTestState *who,
                                             int64_t time_s)
{
    int max_try_count = 10000;
    usleep(time_s * 1000000);

    while (!calc_dirtyrate_ready(who) && max_try_count--) {
        usleep(1000);
    }

    /*
     * Set the timeout with 10 s(max_try_count * 1000us),
     * if dirtyrate measurement not complete, fail test.
     */
    g_assert_cmpint(max_try_count, !=, 0);
}

static int64_t get_dirty_rate(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);
    g_assert_cmpstr(status, ==, "measured");

    rates = qdict_get_qlist(rsp_return, "vcpu-dirty-rate");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "dirty-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static int64_t get_limit_rate(QTestState *who)
{
    QDict *rsp_return;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_vcpu_dirty_limit(who);
    g_assert(rsp_return);

    rates = qdict_get_qlist(rsp_return, "return");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "limit-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static GuestState *dirtylimit_start_vm(void)
{
    GuestState *vm = guest_create("dirtylimit-test");

    guest_use_dirty_ring(vm);

    g_autofree gchar *
    cmd = g_strdup_printf("-accel kvm,dirty-ring-size=4096 "
                          "-name dirtylimit-test,debug-threads=on "
                          "-m 150M -smp 1 "
                          "-serial file:%s "
                          "-drive file=%s,format=raw ",
                          vm->serial_path,
                          bootpath);

    vm->qs = qtest_init(cmd);
    return vm;
}

static void dirtylimit_stop_vm(GuestState *vm)
{
    guest_destroy(vm);
}

static void test_vcpu_dirty_limit(void)
{
    int64_t origin_rate;
    int64_t quota_rate;
    int64_t rate ;
    int max_try_count = 20;
    int hit = 0;

    /* Start vm for vcpu dirtylimit test */
    GuestState *vm = dirtylimit_start_vm();

    /* Wait for the first serial output from the vm*/
    wait_for_serial(vm);

    /* Do dirtyrate measurement with calc time equals 1s */
    calc_dirty_rate(vm->qs, 1);

    /* Sleep calc time and wait for calc dirtyrate complete */
    wait_for_calc_dirtyrate_complete(vm->qs, 1);

    /* Query original dirty page rate */
    origin_rate = get_dirty_rate(vm->qs);

    /* VM booted from bootsect should dirty memory steadily */
    assert(origin_rate != 0);

    /* Setup quota dirty page rate at half of origin */
    quota_rate = origin_rate / 2;

    /* Set dirtylimit */
    dirtylimit_set_all(vm->qs, quota_rate);

    /*
     * Check if set-vcpu-dirty-limit and query-vcpu-dirty-limit
     * works literally
     */
    g_assert_cmpint(quota_rate, ==, get_limit_rate(vm->qs));

    /* Sleep a bit to check if it take effect */
    usleep(2000000);

    /*
     * Check if dirtylimit take effect realistically, set the
     * timeout with 20 s(max_try_count * 1s), if dirtylimit
     * doesn't take effect, fail test.
     */
    while (--max_try_count) {
        calc_dirty_rate(vm->qs, 1);
        wait_for_calc_dirtyrate_complete(vm->qs, 1);
        rate = get_dirty_rate(vm->qs);

        /*
         * Assume hitting if current rate is less
         * than quota rate (within accepting error)
         */
        if (rate < (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);

    hit = 0;
    max_try_count = 20;

    /* Check if dirtylimit cancellation take effect */
    cancel_vcpu_dirty_limit(vm->qs);
    while (--max_try_count) {
        calc_dirty_rate(vm->qs, 1);
        wait_for_calc_dirtyrate_complete(vm->qs, 1);
        rate = get_dirty_rate(vm->qs);

        /*
         * Assume dirtylimit be canceled if current rate is
         * greater than quota rate (within accepting error)
         */
        if (rate > (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);
    dirtylimit_stop_vm(vm);
}

static bool kvm_dirty_ring_supported(void)
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

static bool shm_supported(void)
{
    if (g_file_test("/dev/shm", G_FILE_TEST_IS_DIR)) {
        return true;
    }
    g_test_message("Skipping test: shared memory not available");
    return false;
}

int main(int argc, char **argv)
{
    bool has_kvm, has_tcg;
    bool has_uffd;
    const char *arch;
    g_autoptr(GError) err = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    has_kvm = qtest_has_accel("kvm");
    has_tcg = qtest_has_accel("tcg");

    if (!has_tcg && !has_kvm) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    has_uffd = ufd_version_check();
    arch = qtest_get_arch();

    /*
     * On ppc64, the test only works with kvm-hv, but not with kvm-pr and TCG
     * is touchy due to race conditions on dirty bits (especially on PPC for
     * some reason)
     */
    if (g_str_equal(arch, "ppc64") &&
        (!has_kvm || access("/sys/module/kvm_hv", F_OK))) {
        g_test_message("Skipping test: kvm_hv not available");
        return g_test_run();
    }

    /*
     * Similar to ppc64, s390x seems to be touchy with TCG, so disable it
     * there until the problems are resolved
     */
    if (g_str_equal(arch, "s390x") && !has_kvm) {
        g_test_message("Skipping test: s390x host with KVM is required");
        return g_test_run();
    }

    tmpfs = g_dir_make_tmp("migration-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert(tmpfs);
    bootfile_create(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    if (has_uffd) {
        qtest_add_func("/migration/postcopy/plain", test_postcopy);
        qtest_add_func("/migration/postcopy/recovery/plain",
                       test_postcopy_recovery);
        qtest_add_func("/migration/postcopy/preempt/plain", test_postcopy_preempt);
        qtest_add_func("/migration/postcopy/preempt/recovery/plain",
                       test_postcopy_preempt_recovery);
        if (getenv("QEMU_TEST_FLAKY_TESTS")) {
            qtest_add_func("/migration/postcopy/compress/plain",
                           test_postcopy_compress);
            qtest_add_func("/migration/postcopy/recovery/compress/plain",
                           test_postcopy_recovery_compress);
        }
    }

    qtest_add_func("/migration/bad_dest", test_baddest);
    qtest_add_func("/migration/precopy/unix/plain", test_precopy_unix_plain);
    qtest_add_func("/migration/precopy/unix/xbzrle", test_precopy_unix_xbzrle);
    /*
     * Compression fails from time to time.
     * Put test here but don't enable it until everything is fixed.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        qtest_add_func("/migration/precopy/unix/compress/wait",
                       test_precopy_unix_compress);
        qtest_add_func("/migration/precopy/unix/compress/nowait",
                       test_precopy_unix_compress_nowait);
    }
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/precopy/unix/tls/psk",
                   test_precopy_unix_tls_psk);

    if (has_uffd) {
        /*
         * NOTE: psk test is enough for postcopy, as other types of TLS
         * channels are tested under precopy.  Here what we want to test is the
         * general postcopy path that has TLS channel enabled.
         */
        qtest_add_func("/migration/postcopy/tls/psk", test_postcopy_tls_psk);
        qtest_add_func("/migration/postcopy/recovery/tls/psk",
                       test_postcopy_recovery_tls_psk);
        qtest_add_func("/migration/postcopy/preempt/tls/psk",
                       test_postcopy_preempt_tls_psk);
        qtest_add_func("/migration/postcopy/preempt/recovery/tls/psk",
                       test_postcopy_preempt_all);
    }
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/precopy/unix/tls/x509/default-host",
                   test_precopy_unix_tls_x509_default_host);
    qtest_add_func("/migration/precopy/unix/tls/x509/override-host",
                   test_precopy_unix_tls_x509_override_host);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    qtest_add_func("/migration/precopy/tcp/plain", test_precopy_tcp_plain);
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/precopy/tcp/tls/psk/match",
                   test_precopy_tcp_tls_psk_match);
    qtest_add_func("/migration/precopy/tcp/tls/psk/mismatch",
                   test_precopy_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/precopy/tcp/tls/x509/default-host",
                   test_precopy_tcp_tls_x509_default_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/override-host",
                   test_precopy_tcp_tls_x509_override_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/mismatch-host",
                   test_precopy_tcp_tls_x509_mismatch_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/friendly-client",
                   test_precopy_tcp_tls_x509_friendly_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/hostile-client",
                   test_precopy_tcp_tls_x509_hostile_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/allow-anon-client",
                   test_precopy_tcp_tls_x509_allow_anon_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/reject-anon-client",
                   test_precopy_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    if (shm_supported()) {
        qtest_add_func("/migration/ignore_shared", test_ignore_shared);
    }
#ifndef _WIN32
    qtest_add_func("/migration/fd_proto", test_migrate_fd_proto);
#endif
    qtest_add_func("/migration/validate_uuid", test_validate_uuid);
    qtest_add_func("/migration/validate_uuid_error", test_validate_uuid_error);
    qtest_add_func("/migration/validate_uuid_src_not_set",
                   test_validate_uuid_src_not_set);
    qtest_add_func("/migration/validate_uuid_dst_not_set",
                   test_validate_uuid_dst_not_set);
    /*
     * See explanation why this test is slow on function definition
     */
    if (g_test_slow()) {
        qtest_add_func("/migration/auto_converge", test_migrate_auto_converge);
    }
    qtest_add_func("/migration/multifd/tcp/plain/none",
                   test_multifd_tcp_none);
    qtest_add_func("/migration/multifd/tcp/plain/cancel",
                   test_multifd_tcp_cancel);
    qtest_add_func("/migration/multifd/tcp/plain/zlib",
                   test_multifd_tcp_zlib);
#ifdef CONFIG_ZSTD
    qtest_add_func("/migration/multifd/tcp/plain/zstd",
                   test_multifd_tcp_zstd);
#endif
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/multifd/tcp/tls/psk/match",
                   test_multifd_tcp_tls_psk_match);
    qtest_add_func("/migration/multifd/tcp/tls/psk/mismatch",
                   test_multifd_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/multifd/tcp/tls/x509/default-host",
                   test_multifd_tcp_tls_x509_default_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/override-host",
                   test_multifd_tcp_tls_x509_override_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/mismatch-host",
                   test_multifd_tcp_tls_x509_mismatch_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/allow-anon-client",
                   test_multifd_tcp_tls_x509_allow_anon_client);
    qtest_add_func("/migration/multifd/tcp/tls/x509/reject-anon-client",
                   test_multifd_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    if (g_str_equal(arch, "x86_64") && has_kvm && kvm_dirty_ring_supported()) {
        qtest_add_func("/migration/dirty_ring",
                       test_precopy_unix_dirty_ring);
        qtest_add_func("/migration/vcpu_dirty_limit",
                       test_vcpu_dirty_limit);
    }

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    bootfile_delete();
    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }
    g_free(tmpfs);

    return ret;
}
