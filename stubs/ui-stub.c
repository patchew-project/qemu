#include "qemu/osdep.h"
#include "qemu-common.h"

#include "ui/console.h"
#include "ui/input.h"
#include "ui/qemu-spice.h"

#include "qapi/qapi-types-ui.h"
#include "qapi/qapi-commands-ui.h"

void qmp_screendump(const char *filename, bool has_device, const char *device,
                    bool has_head, int64_t head, Error **errp)
{
    qemu_debug_assert(0);
}

VncInfo *qmp_query_vnc(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

VncInfo2List *qmp_query_vnc_servers(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

MouseInfoList *qmp_query_mice(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

void qmp_send_key(KeyValueList *keys, bool has_hold_time, int64_t hold_time,
                  Error **errp)
{
    qemu_debug_assert(0);
}

void qmp_input_send_event(bool has_device, const char *device,
                          bool has_head, int64_t head,
                          InputEventList *events, Error **errp)
{
    qemu_debug_assert(0);
}

void vnc_display_open(const char *id, Error **errp)
{
    qemu_debug_assert(0);
}

void vnc_display_add_client(const char *id, int csock, bool skipauth)
{
    qemu_debug_assert(0);
}

void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value)
{
    qemu_debug_assert(0);
}

void qemu_input_queue_btn(QemuConsole *src, InputButton btn, bool down)
{
    qemu_debug_assert(0);
}

void qemu_input_event_sync(void)
{
    qemu_debug_assert(0);
}

void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new)
{
    qemu_debug_assert(0);
}

#ifdef CONFIG_SPICE

int using_spice;

SpiceInfo *qmp_query_spice(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

int qemu_spice_display_add_client(int csock, int skipauth, int tls)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

int qemu_spice_set_passwd(const char *passwd, bool fail_if_conn,
                          bool disconnect_if_conn)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

int qemu_spice_set_pw_expire(time_t expires)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

#endif

int index_from_key(const char *key, size_t key_length)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}
