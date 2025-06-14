#define G_GNUC_PRINTF(n, m) __attribute__((format(printf, n, m)))

void my_printf(const char *fmt, ...);
