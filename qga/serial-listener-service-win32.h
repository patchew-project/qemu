/*
 * QEMU Guest Agent helpers for win32 service management
 *
 * Authors:
 *  Sameeh Jubran        <sjubran@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QGA__SERIAL_LISTENER_SERVICE_WIN32_H
#define QGA__SERIAL_LISTENER_SERVICE_WIN32_H

#include <service-win32.h>

#define QGA_SERIAL_LISTENER_SERVICE_DISPLAY_NAME "QEMU Guest Agent Serial \
Listener"
#define QGA_SERIAL_LISTENER_SERVICE_NAME         "QEMU Guest Agent Serial \
Listener"
#define QGA_SERIAL_LISTENER_SERVICE_DESCRIPTION  "Enables running qemu-ga \
service on serial device events"
#define QGA_SERIAL_LISTENER_BINARY_NAME          "qga-serial-listener.exe"

typedef struct GASerialListenerService {
    GAService qga_service;
    HDEVNOTIFY device_notification_handle;
} GASerialListenerService;

#endif
