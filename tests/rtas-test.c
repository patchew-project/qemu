#include <time.h>

#include "qemu/osdep.h"
#include "libqtest.h"

#include "libqos/rtas.h"

static void test_rtas_get_time_of_day(void)
{
    QGuestAllocator *alloc;
    struct tm tm;
    uint32_t ns;
    uint64_t ret;
    time_t t1, t2;

    qtest_start("");

    alloc = machine_alloc_init();

    t1 = time(NULL);
    ret = qrtas_get_time_of_day(alloc, &tm, &ns);
    g_assert_cmpint(ret, ==, 0);
    t2 = timegm(&tm);
    g_assert(t2 - t1 < 5); /* 5 sec max to run the test */

    machine_alloc_uninit(alloc);
    qtest_quit(global_qtest);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "ppc64") == 0) {
        qtest_add_func("rtas/get-time-of-day", test_rtas_get_time_of_day);
    } else {
        g_assert_not_reached();
    }

    return g_test_run();
}
