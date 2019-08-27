/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "ui/input.h"

#define TYPE_INPUT_BARRIER "input-barrier"
#define INPUT_BARRIER(obj) \
    OBJECT_CHECK(InputBarrier, (obj), TYPE_INPUT_BARRIER)
#define INPUT_BARRIER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(InputBarrierClass, (obj), TYPE_INPUT_BARRIER)
#define INPUT_BARRIER_CLASS(klass) \
    OBJECT_CLASS_CHECK(InputBarrierClass, (klass), TYPE_INPUT_BARRIER)

typedef struct InputBarrier InputBarrier;
typedef struct InputBarrierClass InputBarrierClass;

#define MAX_HELLO_LENGTH 1024

struct InputBarrier {
    Object parent;

    QIOChannelSocket *sioc;
    guint ioc_tag;

    /* display properties */
    char *name;

    /*
     * XXX:
     * add x-origin, y-origin, width and height
     * add Barrier server IP address and port
     */

    char buffer[MAX_HELLO_LENGTH];
};

struct InputBarrierClass {
    ObjectClass parent_class;
};

/* Barrier protocol */
#define BARRIER_VERSION_MAJOR 1
#define BARRIER_VERSION_MINOR 6

enum cmdids {
    MSG_CNoop,
    MSG_CClose,
    MSG_CEnter,
    MSG_CLeave,
    MSG_CClipboard,
    MSG_CScreenSaver,
    MSG_CResetOptions,
    MSG_CInfoAck,
    MSG_CKeepAlive,
    MSG_DKeyDown,
    MSG_DKeyRepeat,
    MSG_DKeyUp,
    MSG_DMouseDown,
    MSG_DMouseUp,
    MSG_DMouseMove,
    MSG_DMouseRelMove,
    MSG_DMouseWheel,
    MSG_DClipboard,
    MSG_DInfo,
    MSG_DSetOptions,
    MSG_DFileTransfer,
    MSG_DDragInfo,
    MSG_QInfo,
    MSG_EIncompatible,
    MSG_EBusy,
    MSG_EUnknown,
    MSG_EBad,
    /* connection sequence */
    MSG_Hello,
    MSG_HelloBack,
};

struct version {
    int16_t major;
    int16_t minor;
};

struct mousebutton {
    int8_t buttonid;
};

struct enter {
    int16_t x;
    int16_t y;
    int32_t seqn;
    int16_t modifier;
};

struct mousepos {
    int16_t x;
    int16_t y;
};

struct key {
    int16_t keyid;
    int16_t modifier;
    int16_t button;
};

struct repeat {
    int16_t keyid;
    int16_t modifier;
    int16_t repeat;
    int16_t button;
};

#define MAX_OPTIONS 32
struct set {
    int nb;
    struct {
        int id;
        char nul;
        int value;
    } option[MAX_OPTIONS];
};

struct msg {
    enum cmdids cmd;
    union {
        struct version version;
        struct mousebutton mousebutton;
        struct mousepos mousepos;
        struct enter enter;
        struct key key;
        struct repeat repeat;
        struct set set;
    };
};

const char *cmd_names[] = {
    [MSG_CNoop]          = "CNOP",
    [MSG_CClose]         = "CBYE",
    [MSG_CEnter]         = "CINN",
    [MSG_CLeave]         = "COUT",
    [MSG_CClipboard]     = "CCLP",
    [MSG_CScreenSaver]   = "CSEC",
    [MSG_CResetOptions]  = "CROP",
    [MSG_CInfoAck]       = "CIAK",
    [MSG_CKeepAlive]     = "CALV",
    [MSG_DKeyDown]       = "DKDN",
    [MSG_DKeyRepeat]     = "DKRP",
    [MSG_DKeyUp]         = "DKUP",
    [MSG_DMouseDown]     = "DMDN",
    [MSG_DMouseUp]       = "DMUP",
    [MSG_DMouseMove]     = "DMMV",
    [MSG_DMouseRelMove]  = "DMRM",
    [MSG_DMouseWheel]    = "DMWM",
    [MSG_DClipboard]     = "DCLP",
    [MSG_DInfo]          = "DINF",
    [MSG_DSetOptions]    = "DSOP",
    [MSG_DFileTransfer]  = "DFTR",
    [MSG_DDragInfo]      = "DDRG",
    [MSG_QInfo]          = "QINF",
    [MSG_EIncompatible]  = "EICV",
    [MSG_EBusy]          = "EBSY",
    [MSG_EUnknown]       = "EUNK",
    [MSG_EBad]           = "EBAD",
    [MSG_Hello]          = "Barrier",
    [MSG_HelloBack]      = "Barrier",
};

static char *read_char(char *p, char *v, int *len)
{
    if (*len < sizeof(char)) {
        return NULL;
    }

    *v = *p;
    p += sizeof(char);
    *len -= sizeof(char);

    return p;
}

static char *read_short(char *p, short *v, int *len)
{
    if (*len < sizeof(short)) {
        return NULL;
    }

    *v = ntohs(*(short *)p);
    p += sizeof(short);
    *len -= sizeof(short);

    return p;
}

static char *read_int(char *p, int *v, int *len)
{
    if (*len < sizeof(int)) {
        return NULL;
    }

    *v = ntohl(*(int *)p);
    p += sizeof(int);
    *len -= sizeof(int);

    return p;
}
static int readcmd(InputBarrier *ib, struct msg *msg)
{
    int ret, len, i;
    enum cmdids cmd;
    char *p;

    ret = qio_channel_read(QIO_CHANNEL(ib->sioc), (char *)&len, sizeof(len),
                           NULL);
    if (ret < 0) {
        return -1;
    }

    len = ntohl(len);
    assert(len <= MAX_HELLO_LENGTH);

    ret = qio_channel_read(QIO_CHANNEL(ib->sioc), ib->buffer, len, NULL);
    if (ret < 0) {
        return -1;
    }

    p = ib->buffer;
    if (len >= strlen(cmd_names[MSG_Hello]) &&
        memcmp(p, cmd_names[MSG_Hello], strlen(cmd_names[MSG_Hello])) == 0) {
        cmd = MSG_Hello;
        p += strlen(cmd_names[MSG_Hello]);
        len -= strlen(cmd_names[MSG_Hello]);
    } else {
        for (cmd = 0; cmd < MSG_Hello; cmd++) {
            if (memcmp(ib->buffer, cmd_names[cmd], 4) == 0) {
                break;
            }
        }

        if (cmd == MSG_Hello) {
            return -2;
        }
        p += 4;
        len -= 4;
    }

    msg->cmd = cmd;
    switch (cmd) {
    case MSG_Hello:
        p = read_short(p, &msg->version.major, &len);
        p = read_short(p, &msg->version.minor, &len);
        break;
    case MSG_DSetOptions:
        p = read_int(p, &i, &len);
        if (!p) {
            break;
        }
        msg->set.nb = i / 2;
        assert(msg->set.nb < MAX_OPTIONS);
        i = 0;
        while (len && i < msg->set.nb) {
            p = read_int(p, &msg->set.option[i].id, &len);
            /* it's a string, restore endianness */
            msg->set.option[i].id = htonl(msg->set.option[i].id);
            msg->set.option[i].nul = 0;
            p = read_int(p, &msg->set.option[i].value, &len);
            i++;
        }
        break;
    case MSG_DMouseMove:
    case MSG_DMouseRelMove:
        p = read_short(p, &msg->mousepos.x, &len);
        p = read_short(p, &msg->mousepos.y, &len);
        break;
    case MSG_DKeyDown:
    case MSG_DKeyUp:
        p = read_short(p, &msg->key.keyid, &len);
        p = read_short(p, &msg->key.modifier, &len);
        msg->key.button = 0;
        if (len) {
            p = read_short(p, &msg->key.button, &len);
        }
        break;
    case MSG_DKeyRepeat:
        p = read_short(p, &msg->repeat.keyid, &len);
        p = read_short(p, &msg->repeat.modifier, &len);
        p = read_short(p, &msg->repeat.repeat, &len);
        msg->repeat.button = 0;
        if (len) {
            p = read_short(p, &msg->repeat.button, &len);
        }
        break;
    case MSG_DMouseDown:
    case MSG_DMouseUp:
        p = read_char(p, (char *)&msg->mousebutton.buttonid, &len);
        break;
    case MSG_DMouseWheel:
        p = read_short(p, &msg->mousepos.x, &len);
        msg->mousepos.y = 0;
        if (len) {
            p = read_short(p, &msg->mousepos.y, &len);
        }
        break;
    case MSG_QInfo:
    case MSG_CInfoAck:
    case MSG_CResetOptions:
    case MSG_CEnter:
    case MSG_DClipboard:
    case MSG_CKeepAlive:
    case MSG_CLeave:
    case MSG_CClose:
        break;
    /* Error codes */
    case MSG_EIncompatible:
        p = read_short(p, &msg->version.major, &len);
        p = read_short(p, &msg->version.minor, &len);
        break;
    case MSG_EBusy:
    case MSG_EUnknown:
    case MSG_EBad:
        break;
    default:
        return -2;
    }

    return 0;
}

static int free_space(InputBarrier *ib, char *p)
{
    return MAX_HELLO_LENGTH - (p - ib->buffer);
}

static char *write_short(InputBarrier *ib, char *p, short v)
{
    if (free_space(ib, p) < sizeof(short)) {
        return NULL;
    }

    *(short *)p = htons(v);
    p += sizeof(short);

    return p;
}

static char *write_int(InputBarrier *ib, char *p, int v)
{
    if (free_space(ib, p) < sizeof(int)) {
        return NULL;
    }

    *(int *)p = htonl(v);
    p += sizeof(int);

    return p;
}

static char *write_cmd(InputBarrier *ib, char *p, enum cmdids cmd)
{
    int len;

    len = strlen(cmd_names[cmd]);

    if (len > free_space(ib, p)) {
        return NULL;
    }

    memcpy(p, cmd_names[cmd], len);
    p += len;

    return p;
}

static char *write_string(InputBarrier *ib, char *p, const char *s)
{
    int len;
    int n = free_space(ib, p);

    len = strlen(s);
    if (n < sizeof(len)) {
        return NULL;
    }

    *(int *)p = htonl(len);
    p += sizeof(int);
    n -= sizeof(int);

    if (len > n) {
        return NULL;
    }

    memcpy(p, s, len);
    p += len;

    return p;
}

static int input_barrier_to_qcode(uint16_t keyid)
{
    if (keyid >= 0xE000 && keyid <= 0xEFFF) {
        keyid += 0x1000;
    }

    return qemu_input_map_x11_to_qcode[keyid];
}

static int input_barrier_to_mouse(uint8_t buttonid)
{
    switch (buttonid) {
    case 1: return INPUT_BUTTON_LEFT;
    case 2: return INPUT_BUTTON_MIDDLE;
    case 3: return INPUT_BUTTON_RIGHT;
    case 4: return INPUT_BUTTON_SIDE;
    case 5: return INPUT_BUTTON_EXTRA;
    }
    return 0;
}

static gboolean input_barrier_event(QIOChannel *ioc G_GNUC_UNUSED,
                                    GIOCondition condition, void *opaque)
{
    InputBarrier *ib = opaque;
    struct msg msg;
    char *p;
    int ret, len;

    ret = readcmd(ib, &msg);
    if (ret < 0) {
        return 1;
    }

    p = ib->buffer + sizeof(int);

    switch (msg.cmd) {
    case MSG_Hello:
        if (msg.version.major < BARRIER_VERSION_MAJOR ||
            (msg.version.major == BARRIER_VERSION_MAJOR &&
             msg.version.minor < BARRIER_VERSION_MINOR)) {
            return 1;
        }
        p = write_cmd(ib, p, MSG_HelloBack);
        p = write_short(ib, p, BARRIER_VERSION_MAJOR);
        p = write_short(ib, p, BARRIER_VERSION_MINOR);
        p = write_string(ib, p, ib->name);
        break;
    case MSG_CClose:
        break;
    case MSG_QInfo:
        p = write_cmd(ib, p, MSG_DInfo);
        p = write_short(ib, p, 0);    /* x origin */
        p = write_short(ib, p, 0);    /* y origin */
        p = write_short(ib, p, 1920); /* width */
        p = write_short(ib, p, 1080); /* height */
        p = write_short(ib, p, 0);    /* warpsize (obsolete) */
        p = write_short(ib, p, 0);    /* mouse x */
        p = write_short(ib, p, 0);    /* mouse y */
        break;
    case MSG_CInfoAck:
        break;
    case MSG_CResetOptions:
        /* TODO: reset options */
        break;
    case MSG_DSetOptions:
        /* TODO: set options */
        break;
    case MSG_CEnter:
        break;
    case MSG_DClipboard:
        break;
    case MSG_CKeepAlive:
        p = write_cmd(ib, p, MSG_CKeepAlive);
        break;
    case MSG_DMouseMove:
        qemu_input_queue_abs(NULL, INPUT_AXIS_X, msg.mousepos.x, 0, 1920);
        qemu_input_queue_abs(NULL, INPUT_AXIS_Y, msg.mousepos.y, 0, 1080);
        qemu_input_event_sync();
        break;
    case MSG_DMouseRelMove:
        qemu_input_queue_rel(NULL, INPUT_AXIS_X, msg.mousepos.x);
        qemu_input_queue_rel(NULL, INPUT_AXIS_Y, msg.mousepos.y);
        qemu_input_event_sync();
        break;
    case MSG_CLeave:
        break;
    case MSG_DKeyDown:
        qemu_input_event_send_key_qcode(NULL,
                                        input_barrier_to_qcode(msg.key.keyid),
                                        true);
        break;
    case MSG_DKeyRepeat:
        qemu_input_event_send_key_qcode(NULL,
                                        input_barrier_to_qcode(msg.key.keyid),
                                        false);
        qemu_input_event_send_key_qcode(NULL,
                                        input_barrier_to_qcode(msg.key.keyid),
                                        true);
        break;
    case MSG_DKeyUp:
        qemu_input_event_send_key_qcode(NULL,
                                        input_barrier_to_qcode(msg.key.keyid),
                                        false);
        break;
    case MSG_DMouseDown:
        qemu_input_queue_btn(NULL,
                             input_barrier_to_mouse(msg.mousebutton.buttonid),
                             true);
        qemu_input_event_sync();
        break;
    case MSG_DMouseUp:
        qemu_input_queue_btn(NULL,
                             input_barrier_to_mouse(msg.mousebutton.buttonid),
                             false);
        qemu_input_event_sync();
        break;
    case MSG_DMouseWheel:
        qemu_input_queue_btn(NULL, (msg.mousepos.y > 0) ? INPUT_BUTTON_WHEEL_UP
                             : INPUT_BUTTON_WHEEL_DOWN, true);
        qemu_input_event_sync();
        qemu_input_queue_btn(NULL, (msg.mousepos.y > 0) ? INPUT_BUTTON_WHEEL_UP
                             : INPUT_BUTTON_WHEEL_DOWN, false);
        qemu_input_event_sync();
        break;
    default:
        p = write_cmd(ib, p, MSG_EUnknown);
        break;;
    }
    /* write the length of the message */

    len = p - ib->buffer - 4;
    if (len) {
        write_int(ib, ib->buffer, len);
        ret = qio_channel_write(QIO_CHANNEL(ib->sioc), ib->buffer,
                                len + 4, NULL);
    }

    return true;
}

static void input_barrier_complete(UserCreatable *uc, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(uc);
    Error *local_err = NULL;
    SocketAddress addr;

    addr.type = SOCKET_ADDRESS_TYPE_INET;
    addr.u.inet.host = g_strdup("localhost");
    addr.u.inet.port = g_strdup("24800");

    /*
     * Connect to the primary
     * Primary is the server where the keyboard and the mouse
     * are connected and forwarded to the secondary (the client)
     */

    ib->sioc = qio_channel_socket_new();
    qio_channel_set_name(QIO_CHANNEL(ib->sioc), "barrier-client");

    qio_channel_socket_connect_sync(ib->sioc, &addr, &local_err);
    if (local_err) {
        object_unref(OBJECT(ib->sioc));
        error_propagate(errp, local_err);
        return;
    }

    qio_channel_set_delay(QIO_CHANNEL(ib->sioc), false);

    ib->ioc_tag = qio_channel_add_watch(QIO_CHANNEL(ib->sioc), G_IO_IN,
                                        input_barrier_event, ib, NULL);
}

static void input_barrier_instance_finalize(Object *obj)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    g_source_remove(ib->ioc_tag);
    g_free(ib->name);
}

static char *input_barrier_get_name(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup(ib->name);
}

static void input_barrier_set_name(Object *obj, const char *value,
                                  Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    if (ib->name) {
        error_setg(errp, "name property already set");
        return;
    }
    ib->name = g_strdup(value);
}

static void input_barrier_instance_init(Object *obj)
{
    object_property_add_str(obj, "name",
                            input_barrier_get_name,
                            input_barrier_set_name, NULL);
}

static void input_barrier_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = input_barrier_complete;
}

static const TypeInfo input_barrier_info = {
    .name = TYPE_INPUT_BARRIER,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(InputBarrierClass),
    .class_init = input_barrier_class_init,
    .instance_size = sizeof(InputBarrier),
    .instance_init = input_barrier_instance_init,
    .instance_finalize = input_barrier_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&input_barrier_info);
}

type_init(register_types);
