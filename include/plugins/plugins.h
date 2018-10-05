#ifndef PLUGINS_INTERFACE_H
#define PLUGINS_INTERFACE_H

#include <stdbool.h>
#include <stdio.h>

/* Mandatory Plugin interfaces */

bool plugin_init(const char *args);
char *plugin_status(void);

/* Other optional hooks are defined by available trace-points */

#endif /* PLUGINS_INTERFACE_H */
