#ifndef CHAR_FD_H
#define CHAR_FD_H

#include "io/channel.h"
#include "sysemu/char.h"

typedef struct FDChardev {
    Chardev parent;
    Chardev *chr;
    QIOChannel *ioc_in, *ioc_out;
    int max_size;
} FDChardev;

#define TYPE_CHARDEV_FD "chardev-fd"

#define FD_CHARDEV(obj) OBJECT_CHECK(FDChardev, (obj), TYPE_CHARDEV_FD)

void qemu_chr_open_fd(Chardev *chr, int fd_in, int fd_out);
int qmp_chardev_open_file_source(char *src, int flags, Error **errp);

#endif /* CHAR_FD_H */
