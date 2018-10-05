#ifndef PLUGINS_H
#define PLUGINS_H

#ifdef CONFIG_TRACE_PLUGIN

void qemu_plugin_parse_cmd_args(const char *optarg);
void qemu_plugin_load(const char *filename, const char *args);
void qemu_plugins_init(void);

#else

static inline void qemu_plugin_parse_cmd_args(const char *optarg) { }
static inline void qemu_plugin_load(const char *filename, const char *args) { }
static inline void qemu_plugins_init(void) { }

#endif

#endif /* PLUGINS_H */
