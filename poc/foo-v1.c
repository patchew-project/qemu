#include <stdio.h>
#include <stdarg.h>
#include "foo-v2.h"

void G_GNUC_PRINTF(1, 2) my_printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
