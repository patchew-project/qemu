#ifndef CHAR_WIN_H
#define CHAR_WIN_H

#include "sysemu/char.h"

typedef struct {
    Chardev parent;
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv;
    BOOL fpipe;
    DWORD len;

    /* Protected by the Chardev chr_write_lock.  */
    OVERLAPPED osend;
    /* FIXME: file/console do not finalize */
    BOOL skip_free;
} WinChardev;

#define NSENDBUF 2048
#define NRECVBUF 2048

#define TYPE_CHARDEV_WIN "chardev-win"
#define WIN_CHARDEV(obj) OBJECT_CHECK(WinChardev, (obj), TYPE_CHARDEV_WIN)

void qemu_chr_open_win_file(Chardev *chr, HANDLE fd_out);
int win_chr_init(Chardev *chr, const char *filename, Error **errp);
int win_chr_pipe_poll(void *opaque);

#endif /* CHAR_WIN_H */
