#define G_GNUC_PRINTF(n, m) __attribute__((format(printf, n, m)))

void G_GNUC_PRINTF(1, 2) my_printf(const char *fmt, ...);
