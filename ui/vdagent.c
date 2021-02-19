#include "qemu/osdep.h"
#include "include/qemu-common.h"
#include "chardev/char.h"
#include "hw/qdev-core.h"
#include "qemu/option.h"
#include "ui/console.h"
#include "ui/input.h"
#include "trace.h"

#include "qapi/qapi-types-char.h"
#include "qapi/qapi-types-ui.h"

#include "spice/vd_agent.h"

#define MSGSIZE_MAX (sizeof(VDIChunkHeader) + \
                     sizeof(VDAgentMessage) + \
                     VD_AGENT_MAX_DATA_SIZE)

#define VDAGENT_MOUSE_DEFAULT true

struct VDAgentChardev {
    Chardev parent;

    /* config */
    bool mouse;

    /* guest vdagent */
    uint32_t caps;
    uint8_t msgbuf[MSGSIZE_MAX];
    uint32_t msgsize;

    /* mouse */
    DeviceState mouse_dev;
    uint32_t mouse_x;
    uint32_t mouse_y;
    uint32_t mouse_btn;
    QemuInputHandlerState *mouse_hs;
};
typedef struct VDAgentChardev VDAgentChardev;

#define TYPE_CHARDEV_VDAGENT "chardev-vdagent"

DECLARE_INSTANCE_CHECKER(VDAgentChardev, VDAGENT_CHARDEV,
                         TYPE_CHARDEV_VDAGENT);

/* ------------------------------------------------------------------ */
/* names, for debug logging                                           */

static const char *cap_name[] = {
    [VD_AGENT_CAP_MOUSE_STATE]                    = "mouse-state",
    [VD_AGENT_CAP_MONITORS_CONFIG]                = "monitors-config",
    [VD_AGENT_CAP_REPLY]                          = "reply",
    [VD_AGENT_CAP_CLIPBOARD]                      = "clipboard",
    [VD_AGENT_CAP_DISPLAY_CONFIG]                 = "display-config",
    [VD_AGENT_CAP_CLIPBOARD_BY_DEMAND]            = "clipboard-by-demand",
    [VD_AGENT_CAP_CLIPBOARD_SELECTION]            = "clipboard-selection",
    [VD_AGENT_CAP_SPARSE_MONITORS_CONFIG]         = "sparse-monitors-config",
    [VD_AGENT_CAP_GUEST_LINEEND_LF]               = "guest-lineend-lf",
    [VD_AGENT_CAP_GUEST_LINEEND_CRLF]             = "guest-lineend-crlf",
    [VD_AGENT_CAP_MAX_CLIPBOARD]                  = "max-clipboard",
    [VD_AGENT_CAP_AUDIO_VOLUME_SYNC]              = "audio-volume-sync",
    [VD_AGENT_CAP_MONITORS_CONFIG_POSITION]       = "monitors-config-position",
    [VD_AGENT_CAP_FILE_XFER_DISABLED]             = "file-xfer-disabled",
    [VD_AGENT_CAP_FILE_XFER_DETAILED_ERRORS]      = "file-xfer-detailed-errors",
#if 0
    [VD_AGENT_CAP_GRAPHICS_DEVICE_INFO]           = "graphics-device-info",
    [VD_AGENT_CAP_CLIPBOARD_NO_RELEASE_ON_REGRAB] = "clipboard-no-release-on-regrab",
    [VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL]          = "clipboard-grab-serial",
#endif
};

static const char *msg_name[] = {
    [VD_AGENT_MOUSE_STATE]           = "mouse-state",
    [VD_AGENT_MONITORS_CONFIG]       = "monitors-config",
    [VD_AGENT_REPLY]                 = "reply",
    [VD_AGENT_CLIPBOARD]             = "clipboard",
    [VD_AGENT_DISPLAY_CONFIG]        = "display-config",
    [VD_AGENT_ANNOUNCE_CAPABILITIES] = "announce-capabilities",
    [VD_AGENT_CLIPBOARD_GRAB]        = "clipboard-grab",
    [VD_AGENT_CLIPBOARD_REQUEST]     = "clipboard-request",
    [VD_AGENT_CLIPBOARD_RELEASE]     = "clipboard-release",
    [VD_AGENT_FILE_XFER_START]       = "file-xfer-start",
    [VD_AGENT_FILE_XFER_STATUS]      = "file-xfer-status",
    [VD_AGENT_FILE_XFER_DATA]        = "file-xfer-data",
    [VD_AGENT_CLIENT_DISCONNECTED]   = "client-disconnected",
    [VD_AGENT_MAX_CLIPBOARD]         = "max-clipboard",
    [VD_AGENT_AUDIO_VOLUME_SYNC]     = "audio-volume-sync",
#if 0
    [VD_AGENT_GRAPHICS_DEVICE_INFO]  = "graphics-device-info",
#endif
};

#define GET_NAME(_m, _v) \
    (((_v) < ARRAY_SIZE(_m) && (_m[_v])) ? (_m[_v]) : "???")

/* ------------------------------------------------------------------ */
/* send messages                                                      */

static void vdagent_send_buf(VDAgentChardev *vd, void *ptr, uint32_t msgsize)
{
    uint8_t *msgbuf = ptr;
    uint32_t len, pos = 0;

    while (pos < msgsize) {
        len = qemu_chr_be_can_write(CHARDEV(vd));
        if (len > msgsize - pos) {
            len = msgsize - pos;
        }
        qemu_chr_be_write(CHARDEV(vd), msgbuf + pos, len);
        pos += len;
    }
}

static void vdagent_send_msg(VDAgentChardev *vd, VDAgentMessage *msg)
{
    uint8_t *msgbuf = (void *)msg;
    uint32_t msgsize = sizeof(VDAgentMessage) + msg->size;
    VDIChunkHeader chunk;

    trace_vdagent_send(GET_NAME(msg_name, msg->type));

    chunk.port = VDP_CLIENT_PORT;
    chunk.size = msgsize;
    vdagent_send_buf(vd, &chunk, sizeof(chunk));

    msg->protocol = VD_AGENT_PROTOCOL;
    vdagent_send_buf(vd, msgbuf, msgsize);
    g_free(msg);
}

static void vdagent_send_caps(VDAgentChardev *vd)
{
    VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                    sizeof(VDAgentAnnounceCapabilities) +
                                    sizeof(uint32_t));
    VDAgentAnnounceCapabilities *caps = (void *)msg->data;

    msg->type = VD_AGENT_ANNOUNCE_CAPABILITIES;
    msg->size = sizeof(VDAgentAnnounceCapabilities) + sizeof(uint32_t);
    if (vd->mouse) {
        caps->caps[0] |= (1 << VD_AGENT_CAP_MOUSE_STATE);
    }

    vdagent_send_msg(vd, msg);
}

/* ------------------------------------------------------------------ */
/* mouse events                                                       */

static void vdagent_send_mouse(VDAgentChardev *vd)
{
    VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                    sizeof(VDAgentMouseState));
    VDAgentMouseState *mouse = (void *)msg->data;

    msg->type = VD_AGENT_MOUSE_STATE;
    msg->size = sizeof(VDAgentMouseState);

    mouse->x       = vd->mouse_x;
    mouse->y       = vd->mouse_y;
    mouse->buttons = vd->mouse_btn;

    vdagent_send_msg(vd, msg);
}

static void vdagent_pointer_event(DeviceState *dev, QemuConsole *src,
                                  InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]        = VD_AGENT_LBUTTON_MASK,
        [INPUT_BUTTON_RIGHT]       = VD_AGENT_RBUTTON_MASK,
        [INPUT_BUTTON_MIDDLE]      = VD_AGENT_MBUTTON_MASK,
        [INPUT_BUTTON_WHEEL_UP]    = VD_AGENT_UBUTTON_MASK,
        [INPUT_BUTTON_WHEEL_DOWN]  = VD_AGENT_DBUTTON_MASK,
#if 0
        [INPUT_BUTTON_SIDE]        = VD_AGENT_SBUTTON_MASK,
        [INPUT_BUTTON_EXTRA]       = VD_AGENT_EBUTTON_MASK,
#endif
    };

    VDAgentChardev *vd = container_of(dev, struct VDAgentChardev, mouse_dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;
    uint32_t xres, yres;

    switch (evt->type) {
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        xres = qemu_console_get_width(src, 1024);
        yres = qemu_console_get_height(src, 768);
        if (move->axis == INPUT_AXIS_X) {
            vd->mouse_x = qemu_input_scale_axis(move->value,
                                                INPUT_EVENT_ABS_MIN,
                                                INPUT_EVENT_ABS_MAX,
                                                0, xres);
        } else if (move->axis == INPUT_AXIS_Y) {
            vd->mouse_y = qemu_input_scale_axis(move->value,
                                                INPUT_EVENT_ABS_MIN,
                                                INPUT_EVENT_ABS_MAX,
                                                0, yres);
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            vd->mouse_btn |= bmap[btn->button];
        } else {
            vd->mouse_btn &= ~bmap[btn->button];
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void vdagent_pointer_sync(DeviceState *dev)
{
    VDAgentChardev *vd = container_of(dev, struct VDAgentChardev, mouse_dev);

    if (vd->caps & (1 << VD_AGENT_CAP_MOUSE_STATE)) {
        vdagent_send_mouse(vd);
    }
}

static QemuInputHandler vdagent_mouse_handler = {
    .name  = "vdagent mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = vdagent_pointer_event,
    .sync  = vdagent_pointer_sync,
};

/* ------------------------------------------------------------------ */
/* chardev backend                                                    */

static void vdagent_chr_open(Chardev *chr,
                             ChardevBackend *backend,
                             bool *be_opened,
                             Error **errp)
{
    VDAgentChardev *vd = VDAGENT_CHARDEV(chr);
    ChardevVDAgent *cfg = backend->u.vdagent.data;

    vd->mouse = VDAGENT_MOUSE_DEFAULT;
    if (cfg->has_mouse) {
        vd->mouse = cfg->mouse;
    }

    if (vd->mouse) {
        vd->mouse_hs = qemu_input_handler_register(&vd->mouse_dev,
                                                   &vdagent_mouse_handler);
    }

    *be_opened = true;
}

static void vdagent_chr_recv_caps(VDAgentChardev *vd, VDAgentMessage *msg)
{
    VDAgentAnnounceCapabilities *caps = (void *)msg->data;
    int i;

    for (i = 0; i < ARRAY_SIZE(cap_name); i++) {
        if (caps->caps[0] & (1 << i)) {
            trace_vdagent_peer_cap(GET_NAME(cap_name, i));
        }
    }

    vd->caps = caps->caps[0];
    if (caps->request) {
        vdagent_send_caps(vd);
    }
    if (vd->caps & (1 << VD_AGENT_CAP_MOUSE_STATE) && vd->mouse_hs) {
        qemu_input_handler_activate(vd->mouse_hs);
    }
}

static uint32_t vdagent_chr_recv(VDAgentChardev *vd)
{
    VDIChunkHeader *chunk = (void *)vd->msgbuf;
    VDAgentMessage *msg = (void *)vd->msgbuf + sizeof(VDIChunkHeader);

    if (sizeof(VDIChunkHeader) + chunk->size > vd->msgsize) {
        return 0;
    }

    trace_vdagent_recv(GET_NAME(msg_name, msg->type));

    switch (msg->type) {
    case VD_AGENT_ANNOUNCE_CAPABILITIES:
        vdagent_chr_recv_caps(vd, msg);
        break;
    default:
        break;
    }

    return sizeof(VDIChunkHeader) + chunk->size;
}

static int vdagent_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VDAgentChardev *vd = VDAGENT_CHARDEV(chr);
    uint32_t copy, move;

    copy = MSGSIZE_MAX - vd->msgsize;
    if (copy > len) {
        copy = len;
    }

    memcpy(vd->msgbuf + vd->msgsize, buf, copy);
    vd->msgsize += copy;

    while (vd->msgsize > sizeof(VDIChunkHeader)) {
        move = vdagent_chr_recv(vd);
        if (move == 0) {
            break;
        }

        memmove(vd->msgbuf, vd->msgbuf + move, vd->msgsize - move);
        vd->msgsize -= move;
    }

    return copy;
}

static void vdagent_chr_set_fe_open(struct Chardev *chr, int fe_open)
{
    VDAgentChardev *vd = VDAGENT_CHARDEV(chr);

    if (!fe_open) {
        trace_vdagent_close();
        /* reset state */
        vd->msgsize = 0;
        vd->caps = 0;
        if (vd->mouse_hs) {
            qemu_input_handler_deactivate(vd->mouse_hs);
        }
        return;
    }

    trace_vdagent_open();
}

static void vdagent_chr_parse(QemuOpts *opts, ChardevBackend *backend,
                              Error **errp)
{
    ChardevVDAgent *cfg;

    backend->type = CHARDEV_BACKEND_KIND_VDAGENT;
    cfg = backend->u.vdagent.data = g_new0(ChardevVDAgent, 1);
    qemu_chr_parse_common(opts, qapi_ChardevVDAgent_base(cfg));
    cfg->has_mouse = true;
    cfg->mouse = qemu_opt_get_bool(opts, "mouse", VDAGENT_MOUSE_DEFAULT);
}

/* ------------------------------------------------------------------ */

static void vdagent_chr_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse            = vdagent_chr_parse;
    cc->open             = vdagent_chr_open;
    cc->chr_write        = vdagent_chr_write;
    cc->chr_set_fe_open  = vdagent_chr_set_fe_open;
}

static const TypeInfo vdagent_chr_type_info = {
    .name = TYPE_CHARDEV_VDAGENT,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VDAgentChardev),
    .class_init = vdagent_chr_class_init,
};

static void register_types(void)
{
    type_register_static(&vdagent_chr_type_info);
}

type_init(register_types);
