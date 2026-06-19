#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include <termios.h>
#include "qapi/error.h"
#include "qemu/sockets.h"
#include "channel-common.h"
#include "cutils.h"

#ifdef CONFIG_SOLARIS
#include <stropts.h>
#endif

#define GA_CHANNEL_BAUDRATE_DEFAULT B38400 /* for isa-serial channels */

struct GAChannel {
    GAChannelCommon common;
};

static gboolean ga_channel_client_event(GIOChannel *channel,
                                        GIOCondition condition, gpointer data);
static gboolean ga_channel_listen_accept(GIOChannel *channel,
                                         GIOCondition condition, gpointer data);

static GIOChannel *ga_channel_make_gio(int fd)
{
    GIOChannel *ch = g_io_channel_unix_new(fd);
    g_assert(ch);
    return ch;
}

static void ga_channel_listen_add(GAChannel *c, int listen_fd, bool create)
{
    if (create) {
        c->common.listen_channel = ga_channel_make_gio(listen_fd);
    }
    g_io_add_watch(c->common.listen_channel, G_IO_IN,
                   ga_channel_listen_accept, c);
}

static gboolean ga_channel_listen_accept(GIOChannel *channel,
                                         GIOCondition condition, gpointer data)
{
    GAChannel *c = data;
    int client_fd;
    GIOChannel *client_ch;
    bool accepted = false;
    Error *err = NULL;

    g_assert(channel != NULL);

    client_fd = qemu_accept(g_io_channel_unix_get_fd(channel), NULL, NULL);
    if (client_fd == -1) {
        g_warning("error converting fd to gsocket: %s", strerror(errno));
        goto out;
    }
    if (!qemu_set_blocking(client_fd, false, &err)) {
        g_warning("%s", error_get_pretty(err));
        error_free(err);
        goto out;
    }
    client_ch = ga_channel_make_gio(client_fd);
    if (ga_channel_common_client_add(&c->common, client_ch,
                                     ga_channel_client_event, c)) {
        g_warning("error setting up connection");
        ga_channel_common_gio_destroy(client_ch);
        goto out;
    }
    accepted = true;

out:
    /* only accept 1 connection at a time */
    return !accepted;
}

static gboolean ga_channel_client_event(GIOChannel *channel,
                                        GIOCondition condition, gpointer data)
{
    GAChannel *c = data;
    gboolean client_cont;

    g_assert(c);
    if (c->common.event_cb) {
        client_cont = c->common.event_cb(condition, c->common.user_data);
        if (!client_cont) {
            ga_channel_common_client_close(&c->common);
            if (c->common.listen_channel) {
                ga_channel_listen_add(c, 0, false);
            }
            return false;
        }
    }
    return true;
}

static gboolean ga_channel_open(GAChannel *c, const gchar *path,
                                GAChannelMethod method, int fd, Error **errp)
{
    c->common.method = method;

    switch (method) {
    case GA_CHANNEL_VIRTIO_SERIAL: {
        GIOChannel *ch;

        assert(fd < 0);
        fd = qga_open_cloexec(
            path,
#ifndef CONFIG_SOLARIS
            O_ASYNC |
#endif
            O_RDWR | O_NONBLOCK,
            0
        );
        if (fd == -1) {
            error_setg_errno(errp, errno, "error opening channel '%s'", path);
            return false;
        }
#ifdef CONFIG_SOLARIS
        if (ioctl(fd, I_SETSIG, S_OUTPUT | S_INPUT | S_HIPRI) == -1) {
            error_setg_errno(errp, errno, "error setting event mask for channel");
            close(fd);
            return false;
        }
#endif
#ifdef __FreeBSD__
        /*
         * In the default state channel sends echo of every command to a
         * client. The client program doesn't expect this and raises an
         * error. Suppress echo by resetting ECHO terminal flag.
         */
        struct termios tio;
        if (tcgetattr(fd, &tio) < 0) {
            error_setg_errno(errp, errno, "error getting channel termios attrs");
            close(fd);
            return false;
        }
        tio.c_lflag &= ~ECHO;
        if (tcsetattr(fd, TCSAFLUSH, &tio) < 0) {
            error_setg_errno(errp, errno, "error setting channel termios attrs");
            close(fd);
            return false;
        }
#endif /* __FreeBSD__ */
        ch = ga_channel_make_gio(fd);
        if (ga_channel_common_client_add(&c->common, ch,
                                         ga_channel_client_event, c)) {
            error_setg(errp, "error adding channel to main loop");
            ga_channel_common_gio_destroy(ch);
            return false;
        }
        break;
    }
    case GA_CHANNEL_ISA_SERIAL: {
        struct termios tio;
        GIOChannel *ch;

        assert(fd < 0);
        fd = qga_open_cloexec(path, O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
        if (fd == -1) {
            error_setg_errno(errp, errno, "error opening channel '%s'", path);
            return false;
        }
        tcgetattr(fd, &tio);
        /* set up serial port for non-canonical, dumb byte streaming */
        tio.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP |
                         INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY |
                         IMAXBEL);
        tio.c_oflag = 0;
        tio.c_lflag = 0;
        tio.c_cflag |= GA_CHANNEL_BAUDRATE_DEFAULT;
        /* 1 available byte min or reads will block (we'll set non-blocking
         * elsewhere, else we have to deal with read()=0 instead)
         */
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        /* flush everything waiting for read/xmit, it's garbage at this point */
        tcflush(fd, TCIFLUSH);
        tcsetattr(fd, TCSANOW, &tio);
        ch = ga_channel_make_gio(fd);
        if (ga_channel_common_client_add(&c->common, ch,
                                         ga_channel_client_event, c)) {
            error_setg(errp, "error adding channel to main loop");
            ga_channel_common_gio_destroy(ch);
            return false;
        }
        break;
    }
    case GA_CHANNEL_UNIX_LISTEN: {
        if (fd < 0) {
            fd = unix_listen(path, errp);
            if (fd < 0) {
                return false;
            }
        }
        ga_channel_listen_add(c, fd, true);
        break;
    }
    case GA_CHANNEL_VSOCK_LISTEN: {
        if (fd < 0) {
            fd = ga_channel_common_vsock_listen(path, errp);
            if (fd < 0) {
                return false;
            }
        }
        ga_channel_listen_add(c, fd, true);
        break;
    }
    default:
        error_setg(errp, "error binding/listening to specified socket");
        return false;
    }

    return true;
}

GIOStatus ga_channel_write_all(GAChannel *c, const gchar *buf, gsize size)
{
    return ga_channel_common_write_all(&c->common, buf, size);
}

GIOStatus ga_channel_read(GAChannel *c, gchar *buf, gsize size, gsize *count)
{
    return ga_channel_common_read(&c->common, buf, size, count);
}

GAChannel *ga_channel_new(GAChannelMethod method, const gchar *path,
                          int listen_fd, GAChannelCallback cb, gpointer opaque)
{
    Error *err = NULL;
    GAChannel *c = g_new0(GAChannel, 1);
    c->common.event_cb = cb;
    c->common.user_data = opaque;

    if (!ga_channel_open(c, path, method, listen_fd, &err)) {
        g_critical("%s", error_get_pretty(err));
        error_free(err);
        ga_channel_free(c);
        return NULL;
    }

    return c;
}

void ga_channel_free(GAChannel *c)
{
    ga_channel_common_free(&c->common);
    g_free(c);
}
