/*
 * QMP commands related to UI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qmp/qerror.h"
#include "qemu/cutils.h"
#include "ui/console.h"
#include "ui/dbus-display.h"
#include "ui/qemu-spice.h"

void qmp_set_password(SetPasswordOptions *opts, Error **errp)
{
    int rc;

    if (opts->protocol == DISPLAY_PROTOCOL_SPICE) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_passwd(opts->password,
                opts->connected == SET_PASSWORD_ACTION_FAIL,
                opts->connected == SET_PASSWORD_ACTION_DISCONNECT);
    } else {
        assert(opts->protocol == DISPLAY_PROTOCOL_VNC);
        if (opts->connected != SET_PASSWORD_ACTION_KEEP) {
            /* vnc supports "connected=keep" only */
            error_setg(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
        /*
         * Note that setting an empty password will not disable login
         * through this interface.
         */
        rc = vnc_display_password(opts->u.vnc.display, opts->password);
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password");
    }
}

void qmp_expire_password(ExpirePasswordOptions *opts, Error **errp)
{
    time_t when;
    int rc;
    const char *whenstr = opts->time;
    const char *numstr = NULL;
    uint64_t num;

    if (strcmp(whenstr, "now") == 0) {
        when = 0;
    } else if (strcmp(whenstr, "never") == 0) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL);
        numstr = whenstr + 1;
    } else {
        when = 0;
        numstr = whenstr;
    }

    if (numstr) {
        if (qemu_strtou64(numstr, NULL, 10, &num) < 0) {
            error_setg(errp, "Parameter 'time' doesn't take value '%s'",
                       whenstr);
            return;
        }
        when += num;
    }

    if (opts->protocol == DISPLAY_PROTOCOL_SPICE) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_pw_expire(when);
    } else {
        assert(opts->protocol == DISPLAY_PROTOCOL_VNC);
        rc = vnc_display_pw_expire(opts->u.vnc.display, when);
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password expire time");
    }
}

#ifdef CONFIG_VNC
void qmp_change_vnc_password(const char *password, Error **errp)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_setg(errp, "Could not set password");
    }
}
#endif

void qmp_add_client(const char *protocol, const char *fdname,
                    bool has_skipauth, bool skipauth, bool has_tls, bool tls,
                    Error **errp)
{
    Chardev *s;
    int fd;

    fd = monitor_get_fd(monitor_cur(), fdname, errp);
    if (fd < 0) {
        return;
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            close(fd);
            return;
        }
        skipauth = has_skipauth ? skipauth : false;
        tls = has_tls ? tls : false;
        if (qemu_spice.display_add_client(fd, skipauth, tls) < 0) {
            error_setg(errp, "spice failed to add client");
            close(fd);
        }
#ifdef CONFIG_VNC
    } else if (strcmp(protocol, "vnc") == 0) {
        skipauth = has_skipauth ? skipauth : false;
        vnc_display_add_client(NULL, fd, skipauth);
#endif
#ifdef CONFIG_DBUS_DISPLAY
    } else if (strcmp(protocol, "@dbus-display") == 0) {
        if (!qemu_using_dbus_display(errp)) {
            close(fd);
            return;
        }
        if (!qemu_dbus_display.add_client(fd, errp)) {
            close(fd);
            return;
        }
#endif
    } else {
        s = qemu_chr_find(protocol);
        if (!s) {
            error_setg(errp, "protocol '%s' is invalid", protocol);
            close(fd);
            return;
        }
        if (qemu_chr_add_client(s, fd) < 0) {
            error_setg(errp, "failed to add client");
            close(fd);
            return;
        }
    }
}

void qmp_display_reload(DisplayReloadOptions *arg, Error **errp)
{
    switch (arg->type) {
    case DISPLAY_RELOAD_TYPE_VNC:
#ifdef CONFIG_VNC
        if (arg->u.vnc.has_tls_certs && arg->u.vnc.tls_certs) {
            vnc_display_reload_certs(NULL, errp);
        }
#else
        error_setg(errp, "vnc is invalid, missing 'CONFIG_VNC'");
#endif
        break;
    default:
        abort();
    }
}

void qmp_display_update(DisplayUpdateOptions *arg, Error **errp)
{
    switch (arg->type) {
    case DISPLAY_UPDATE_TYPE_VNC:
#ifdef CONFIG_VNC
        vnc_display_update(&arg->u.vnc, errp);
#else
        error_setg(errp, "vnc is invalid, missing 'CONFIG_VNC'");
#endif
        break;
    default:
        abort();
    }
}
