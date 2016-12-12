#ifndef CHAR_IO_H
#define CHAR_IO_H

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "io/channel.h"
#include "sysemu/char.h"

/* Can only be used for read */
guint io_add_watch_poll(Chardev *chr,
                        QIOChannel *ioc,
                        IOCanReadHandler *fd_can_read,
                        QIOChannelFunc fd_read,
                        gpointer user_data,
                        GMainContext *context);

void remove_fd_in_watch(Chardev *chr);

int io_channel_send(QIOChannel *ioc, const void *buf, size_t len);

int io_channel_send_full(QIOChannel *ioc, const void *buf, size_t len,
                         int *fds, size_t nfds);

#endif /* CHAR_IO_H */
