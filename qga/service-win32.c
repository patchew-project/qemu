/*
 * QEMU Guest Agent helpers for win32 service management
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Gal Hammer        <ghammer@redhat.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <windows.h>
#include "shlwapi.h"
#include "qga/service-win32.h"
#include "qga/serial-listener-service-win32.h"

static int printf_win_error(const char *text)
{
    DWORD err = GetLastError();
    char *message;
    int n;

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char *)&message, 0,
        NULL);
    n = fprintf(stderr, "%s. (Error: %d) %s", text, (int)err, message);
    LocalFree(message);

    return n;
}

/* Windows command line escaping. Based on
 * <http://blogs.msdn.com/b/oldnewthing/archive/2010/09/17/10063629.aspx> and
 * <http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft%28v=vs.85%29.aspx>.
 *
 * The caller is responsible for initializing @buffer; prior contents are lost.
 */
static const char *win_escape_arg(const char *to_escape, GString *buffer)
{
    size_t backslash_count;
    const char *c;

    /* open with a double quote */
    g_string_assign(buffer, "\"");

    backslash_count = 0;
    for (c = to_escape; *c != '\0'; ++c) {
        switch (*c) {
        case '\\':
            /* The meaning depends on the first non-backslash character coming
             * up.
             */
            ++backslash_count;
            break;

        case '"':
            /* We must escape each pending backslash, then escape the double
             * quote. This creates a case of "odd number of backslashes [...]
             * followed by a double quotation mark".
             */
            while (backslash_count) {
                --backslash_count;
                g_string_append(buffer, "\\\\");
            }
            g_string_append(buffer, "\\\"");
            break;

        default:
            /* Any pending backslashes are without special meaning, flush them.
             * "Backslashes are interpreted literally, unless they immediately
             * precede a double quotation mark."
             */
            while (backslash_count) {
                --backslash_count;
                g_string_append_c(buffer, '\\');
            }
            g_string_append_c(buffer, *c);
        }
    }

    /* We're about to close with a double quote in string delimiter role.
     * Double all pending backslashes, creating a case of "even number of
     * backslashes [...] followed by a double quotation mark".
     */
    while (backslash_count) {
        --backslash_count;
        g_string_append(buffer, "\\\\");
    }
    g_string_append_c(buffer, '"');

    return buffer->str;
}


static int get_service(const char *service_name, SC_HANDLE* service)
{
    SC_HANDLE manager = NULL;
    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL) {
        printf_win_error("No handle to service control manager");
        return EXIT_FAILURE;
    }

    *service = OpenService(manager, service_name, SERVICE_ALL_ACCESS);
    if (service == NULL) {
        printf_win_error("Failed to open service");
        CloseServiceHandle(manager);
        return EXIT_FAILURE;
    }

    CloseServiceHandle(manager);
    return EXIT_SUCCESS;
}

static int install_service(const char *path, const char *logfile,
    const char *state_dir, TCHAR *binary_path, LPCTSTR service_name,
    LPCTSTR service_display_name, SERVICE_DESCRIPTION service_desc,
    bool start_service)
{
    int ret = EXIT_FAILURE;
    SC_HANDLE manager;
    SC_HANDLE service;
    GString *esc;
    GString *cmdline;


    esc     = g_string_new("");
    cmdline = g_string_new("");

    g_string_append_printf(cmdline, "%s -d",
                           win_escape_arg(binary_path, esc));

    if (path) {
        g_string_append_printf(cmdline, " -p %s", win_escape_arg(path, esc));
    }
    if (logfile) {
        g_string_append_printf(cmdline, " -l %s -v",
                               win_escape_arg(logfile, esc));
    }
    if (state_dir) {
        g_string_append_printf(cmdline, " -t %s",
                               win_escape_arg(state_dir, esc));
    }

    g_debug("service's cmdline: %s", cmdline->str);

    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL) {
        printf_win_error("No handle to service control manager");
        goto out_strings;
    }

    service = CreateService(manager, service_name, service_display_name,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, cmdline->str, NULL, NULL, NULL, NULL, NULL);
    if (service == NULL) {
        printf_win_error("Failed to install service");
        goto out_manager;
    }

    ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &service_desc);
    fprintf(stderr, "Service was installed successfully.\n");

    if (start_service && StartService(service, 0, NULL)) {
        fprintf(stderr, "Service was started successfully.\n");
    }
    ret = EXIT_SUCCESS;
    CloseServiceHandle(service);

out_manager:
    CloseServiceHandle(manager);

out_strings:
    g_string_free(cmdline, TRUE);
    g_string_free(esc, TRUE);
    return ret;
}

int ga_install_service(const char *path, const char *logfile,
                       const char *state_dir)
{
    TCHAR module_fname[MAX_PATH];

    if (GetModuleFileName(NULL, module_fname, MAX_PATH) == 0) {
        printf_win_error("No full path to service's executable");
        return EXIT_FAILURE;
    }
    SERVICE_DESCRIPTION service_desc = { (char *)QGA_SERVICE_DESCRIPTION };
    return install_service(path, logfile, state_dir, module_fname,
        QGA_SERVICE_NAME, QGA_SERVICE_DISPLAY_NAME, service_desc, true);
}

static int uninstall_service(LPCTSTR service_name)
{
    SC_HANDLE manager;
    SC_HANDLE service;

    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL) {
        printf_win_error("No handle to service control manager");
        return EXIT_FAILURE;
    }

    service = OpenService(manager, service_name, DELETE);
    if (service == NULL) {
        printf_win_error("No handle to service");
        CloseServiceHandle(manager);
        return EXIT_FAILURE;
    }

    if (DeleteService(service) == FALSE) {
        printf_win_error("Failed to delete service");
    } else {
        fprintf(stderr, "Service was deleted successfully.\n");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    return EXIT_SUCCESS;
}

int ga_uninstall_service(void)
{
    return uninstall_service(QGA_SERVICE_NAME);
}

int ga_install_serial_listener_service(const char *path, const char *logfile,
    const char *state_dir)
{
    TCHAR module_fname[MAX_PATH];
    TCHAR binary_path[MAX_PATH];

    if (GetModuleFileName(NULL, module_fname, MAX_PATH) == 0) {
        printf_win_error("No full path to service's executable");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "ga_install_serial_listener_service: module name: %s\n",
        module_fname);
    PathRemoveFileSpec(module_fname);
    if (SearchPath(module_fname, QGA_SERIAL_LISTENER_BINARY_NAME, NULL,
        MAX_PATH, binary_path, NULL) == 0) {
        printf_win_error("No full path to service's executable");
        return EXIT_FAILURE;
    }

    SERVICE_DESCRIPTION service_desc = {
        (char *)QGA_SERIAL_LISTENER_SERVICE_DESCRIPTION };
    return install_service(path, logfile, state_dir, binary_path,
        QGA_SERIAL_LISTENER_SERVICE_NAME,
        QGA_SERIAL_LISTENER_SERVICE_DISPLAY_NAME, service_desc, true);
}

int ga_uninstall_serial_listener_service(void)
{
    return uninstall_service(QGA_SERIAL_LISTENER_SERVICE_NAME);
}

int start_service(const char *service_name)
{
    int ret = EXIT_FAILURE;
    SC_HANDLE service = NULL;
    ret = get_service(service_name, &service);
    if (ret != EXIT_SUCCESS) {
        return ret;
    }
    ret = StartService(service, 0 , NULL) ? EXIT_SUCCESS : GetLastError();

    CloseServiceHandle(service);
    return ret;
}

int stop_service(const char *service_name)
{
    int ret = EXIT_FAILURE;
    SC_HANDLE service = NULL;

    SERVICE_STATUS service_status;
    ret = get_service(service_name, &service);

    if (ret != EXIT_SUCCESS) {
        return ret;
    }
    ret = ControlService(service, SERVICE_CONTROL_STOP, &service_status) ?
        EXIT_SUCCESS : GetLastError();

    CloseServiceHandle(service);
    return ret;
}
