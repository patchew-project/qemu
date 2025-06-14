#include <stdio.h>
#include <stdarg.h>
#include "foo-v1.h"

void my_printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
