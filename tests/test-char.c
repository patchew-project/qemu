#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/config-file.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qmp-commands.h"

typedef struct FeHandler {
    char read_buf[128];
    int read_count;
    int last_event;
} FeHandler;

static int fe_can_read(void *opaque)
{
    FeHandler *h = opaque;

    return sizeof(h->read_buf) - h->read_count;
}

static void fe_read(void *opaque, const uint8_t *buf, int size)
{
    FeHandler *h = opaque;

    g_assert_cmpint(size, <=, fe_can_read(opaque));

    memcpy(h->read_buf + h->read_count, buf, size);
    h->read_count += size;
}

static void fe_event(void *opaque, int event)
{
    FeHandler *h = opaque;

    h->last_event = event;
}

#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
static void char_stdio_test_subprocess(void)
{
    CharDriverState *chr;
    int ret;

    chr = qemu_chr_new("label", "stdio", NULL);
    g_assert_nonnull(chr);

    qemu_chr_fe_set_open(chr, true);
    ret = qemu_chr_fe_write(chr, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_delete(chr);
}

static void char_stdio_test(void)
{
    g_test_trap_subprocess("/char/stdio/subprocess", 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stdout("buf");
}
#endif


static void char_ringbuf_test(void)
{
    QemuOpts *opts;
    CharDriverState *chr;
    char *data;
    int ret;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);

    qemu_opt_set(opts, "size", "5", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
    g_assert_null(chr);
    qemu_opts_del(opts);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "2", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    ret = qemu_chr_fe_write(chr, (void *)"buff", 4);
    g_assert_cmpint(ret, ==, 4);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "ff");
    g_free(data);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "");
    g_free(data);

    qemu_chr_delete(chr);
}

static void char_mux_test(void)
{
    QemuOpts *opts;
    CharDriverState *chr, *base;
    char *data;
    int tag1, tag2;
    FeHandler h1 = { 0, }, h2 = { 0, };

    opts = qemu_opts_create(qemu_find_opts("chardev"), "mux-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "128", &error_abort);
    qemu_opt_set(opts, "mux", "on", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    tag1 = qemu_chr_add_handlers(chr,
                                 fe_can_read,
                                 fe_read,
                                 fe_event,
                                 &h1,
                                 NULL,
                                 &error_abort);
    g_assert_cmpint(tag1, !=, -1);

    tag2 = qemu_chr_add_handlers(chr,
                                 fe_can_read,
                                 fe_read,
                                 fe_event,
                                 &h2,
                                 NULL,
                                 &error_abort);
    g_assert_cmpint(tag2, !=, -1);

    g_assert_cmpint(qemu_chr_be_can_write(chr), !=, 0);

    base = qemu_chr_find("mux-label-base");

    /* the last handler has the focus */
    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 6);
    g_assert_cmpstr(h2.read_buf, ==, "hello");
    h2.read_count = 0;

    /* switch focus */
    qemu_chr_be_write(base, (void *)"\1c", 2);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h2.read_count, ==, 0);
    g_assert_cmpint(h1.read_count, ==, 6);
    g_assert_cmpstr(h1.read_buf, ==, "hello");
    h1.read_count = 0;

    /* remove first handler */
    qemu_chr_remove_handlers(chr, tag1);
    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 0);

    qemu_chr_be_write(base, (void *)"\1c", 2);
    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 6);
    g_assert_cmpstr(h2.read_buf, ==, "hello");
    h2.read_count = 0;

    /* print help */
    qemu_chr_be_write(base, (void *)"\1?", 2);
    data = qmp_ringbuf_read("mux-label-base", 128, false, 0, &error_abort);
    g_assert_cmpint(strlen(data), !=, 0);
    g_free(data);

    qemu_chr_remove_handlers(chr, tag1);
    qemu_chr_remove_handlers(chr, tag2);
    qemu_chr_delete(chr);
}

static void char_null_test(void)
{
    CharDriverState *chr;
    int ret, tag;

    chr = qemu_chr_find("label-null");
    g_assert_null(chr);

    chr = qemu_chr_new("label-null", "null", NULL);
    chr = qemu_chr_find("label-null");
    g_assert_nonnull(chr);

    qemu_chr_fe_claim_no_fail(chr);
    ret = qemu_chr_fe_claim(chr);
    g_assert_cmpint(ret, ==, -1);

    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_FD_PASS) == false);
    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_RECONNECTABLE) == false);

    qemu_chr_fe_set_open(chr, true);

    tag = qemu_chr_add_handlers(chr,
                                fe_can_read,
                                fe_read,
                                fe_event,
                                NULL,
                                NULL,
                                &error_abort);
    g_assert_cmpint(tag, !=, -1);

    ret = qemu_chr_fe_write(chr, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_remove_handlers(chr, tag);
    qemu_chr_fe_release(chr);
    qemu_chr_delete(chr);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    g_test_add_func("/char/null", char_null_test);
    g_test_add_func("/char/ringbuf", char_ringbuf_test);
    g_test_add_func("/char/mux", char_mux_test);
#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
    g_test_add_func("/char/stdio/subprocess", char_stdio_test_subprocess);
    g_test_add_func("/char/stdio", char_stdio_test);
#endif

    return g_test_run();
}
