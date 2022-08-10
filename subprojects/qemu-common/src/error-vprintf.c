#include <stdio.h>
#include "qemu/error-report.h"

int error_vprintf(const char *fmt, va_list ap)
{
    int ret;

    if (g_test_initialized() && !g_test_subprocess() &&
        g_getenv("QTEST_SILENT_ERRORS")) {
        char *msg = g_strdup_vprintf(fmt, ap);
        g_test_message("%s", msg);
        ret = strlen(msg);
        g_free(msg);
        return ret;
    }
    return vfprintf(stderr, fmt, ap);
}
