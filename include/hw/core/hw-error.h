#ifndef HW_CORE_HW_ERROR_H
#define HW_CORE_HW_ERROR_H

G_NORETURN void hw_error(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

#endif
