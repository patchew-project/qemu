/*
 * QEMU Guest Agent helpers for win32 service management
 *
 * Authors:
 *  Sameeh Jubran        <sjubran@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <windows.h>
#include <dbt.h>
#include "qga/channel.h"
#include "qga/serial-listener-service-win32.h"

static const GUID GUID_VIOSERIAL_PORT = { 0x6fde7521, 0x1b65, 0x48ae,
    { 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26 } };

GASerialListenerService listener_service;
GMainLoop *main_loop;
bool barrier;
bool qga_vios_exists;

DWORD WINAPI handle_serial_device_events(DWORD type, LPVOID data);
DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data,
    LPVOID ctx);
VOID WINAPI service_main(DWORD argc, TCHAR * argv[]);
static void quit_handler(int sig);
static bool virtio_serial_exists(DWORD *err);

static bool virtio_serial_exists(DWORD *err)
{
    bool ret = false;
    HANDLE handle = CreateFile(QGA_VIRTIO_PATH_DEFAULT, GENERIC_READ |
        GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING |
        FILE_FLAG_OVERLAPPED, NULL);

    if (handle == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        ret = false;
    } else {
        ret = true;
    }
    CloseHandle(handle);
    return ret;
}

static void quit_handler(int sig)
{
    if (g_main_loop_is_running(main_loop)) {
        g_main_loop_quit(main_loop);
    }
}

DWORD WINAPI handle_serial_device_events(DWORD type, LPVOID data)
{
    DWORD ret = NO_ERROR;
    DWORD err_code;
    PDEV_BROADCAST_HDR broadcast_header = (PDEV_BROADCAST_HDR) data;
    if (barrier &&
        broadcast_header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
        switch (type) {
            /* Device inserted */
        case DBT_DEVICEARRIVAL:
            /* Start QEMU-ga's service */
            if (!qga_vios_exists && virtio_serial_exists(&err_code)) {
                start_service(QGA_SERVICE_NAME);
                qga_vios_exists = true;
            }
            break;
            /* Device removed */
        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEREMOVEPENDING:
        case DBT_DEVICEREMOVECOMPLETE:
            /* Stop QEMU-ga's service */
            /* In a stop operation, we need to make sure the virtio-serial that
            * qemu-ga uses is the one that is being ejected. We'll get the error
            * ERROR_FILE_NOT_FOUND when attempting to call CreateFile with the
            * virtio-serial path.
            */
            if (qga_vios_exists && !virtio_serial_exists(&err_code) &&
                err_code == ERROR_FILE_NOT_FOUND) {
                stop_service(QGA_SERVICE_NAME);
                qga_vios_exists = false;
            }

            break;
        default:
            ret = ERROR_CALL_NOT_IMPLEMENTED;
        }
    }
        return ret;
}

DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data,
    LPVOID ctx)
{
    DWORD ret = NO_ERROR;

    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        quit_handler(SIGTERM);
        listener_service.qga_service.status.dwCurrentState =
            SERVICE_STOP_PENDING;
        SetServiceStatus(listener_service.qga_service.status_handle,
            &listener_service.qga_service.status);
    case SERVICE_CONTROL_DEVICEEVENT:
            handle_serial_device_events(type, data);
        break;

    default:
        ret = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return ret;
}

VOID WINAPI service_main(DWORD argc, TCHAR * argv[])
{
    DWORD err_code = NO_ERROR;
    qga_vios_exists = barrier = false;
    listener_service.qga_service.status_handle =
        RegisterServiceCtrlHandlerEx(QGA_SERIAL_LISTENER_SERVICE_NAME,
        service_ctrl_handler, NULL);

    if (listener_service.qga_service.status_handle == 0) {
        g_critical("Failed to register extended requests function!\n");
        return;
    }

    listener_service.qga_service.status.dwServiceType = SERVICE_WIN32;
    listener_service.qga_service.status.dwCurrentState = SERVICE_RUNNING;
    listener_service.qga_service.status.dwControlsAccepted = SERVICE_ACCEPT_STOP
        | SERVICE_ACCEPT_SHUTDOWN;
    listener_service.qga_service.status.dwWin32ExitCode = NO_ERROR;
    listener_service.qga_service.status.dwServiceSpecificExitCode = NO_ERROR;
    listener_service.qga_service.status.dwCheckPoint = 0;
    listener_service.qga_service.status.dwWaitHint = 0;
    SetServiceStatus(listener_service.qga_service.status_handle,
        &listener_service.qga_service.status);

    DEV_BROADCAST_DEVICEINTERFACE notification_filter;
    ZeroMemory(&notification_filter, sizeof(notification_filter));
        notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
        notification_filter.dbcc_classguid = GUID_VIOSERIAL_PORT;

    listener_service.device_notification_handle =
        RegisterDeviceNotification(listener_service.qga_service.status_handle,
        &notification_filter, DEVICE_NOTIFY_SERVICE_HANDLE);
    if (!listener_service.device_notification_handle) {
        g_critical("Failed to register device notification handle!\n");
        return;
    }

    qga_vios_exists = virtio_serial_exists(&err_code);
    /* In case qemu-ga is already running then Create file will return Access
    * denied when trying to open the virtio serial
    */
    if (!qga_vios_exists && err_code == ERROR_ACCESS_DENIED) {
        qga_vios_exists = true;
    }
    barrier = true;

    main_loop = g_main_loop_new(NULL, false);
    g_main_loop_run(main_loop);
    UnregisterDeviceNotification(listener_service.device_notification_handle);
    listener_service.qga_service.status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(listener_service.qga_service.status_handle,
        &listener_service.qga_service.status);
}

int main(int argc, char **argv)
{
    SERVICE_TABLE_ENTRY service_table[] = {
        { (char *)QGA_SERIAL_LISTENER_SERVICE_NAME, service_main },
        { NULL, NULL } };
    StartServiceCtrlDispatcher(service_table);

    return EXIT_SUCCESS;
}
