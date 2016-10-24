#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

void error_vprintf(const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
}

void error_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
}

void error_printf_unless_qmp(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
}
