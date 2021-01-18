#ifndef QEMU_CONFIG_FILE_H
#define QEMU_CONFIG_FILE_H


QemuOptsList *qemu_find_opts(const char *group);
QemuOptsList *qemu_find_opts_err(const char *group, Error **errp);
QemuOpts *qemu_find_opts_singleton(const char *group);

void qemu_add_opts(QemuOptsList *list);
void qemu_add_drive_opts(QemuOptsList *list);
int qemu_global_option(const char *str);

int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname, Error **errp);

int qemu_read_config_file(const char *filename, Error **errp);

/* Parse QDict options as a replacement for a config file (allowing multiple
   enumerated (0..(n-1)) configuration "sections") */
void qemu_config_parse_qdict(QDict *options, QemuOptsList **lists,
                             Error **errp);

#endif /* QEMU_CONFIG_FILE_H */
