#include "hw/sysbus.h"

void measurements_extend_data(int pcrnum, uint8_t *data, size_t len, char *description);
void measurements_set_log(gchar *log);
