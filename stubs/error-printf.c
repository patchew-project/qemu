#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

int error_vprintf(const char *fmt, va_list ap)
{
    if (g_test_initialized() && !g_test_subprocess() &&
        getenv("QTEST_SILENT_ERRORS")) {
        char *msg = g_strdup_vprintf(fmt, ap);
        g_test_message("%s", msg);
        g_free(msg);
        return strlen(msg);
    }
    return vfprintf(stderr, fmt, ap);
}

int error_vprintf_unless_qmp(const char *fmt, va_list ap)
{
    return error_vprintf(fmt, ap);
}
