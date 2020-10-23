/*
 * replay-input.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "qemu/notify.h"
#include "ui/input.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-ui.h"

void replay_save_input_event(InputEvent *evt)
{
    InputKeyEvent *key;
    InputBtnEvent *btn;
    InputMoveEvent *move;
    replay_put_dword(evt->type);

    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = &evt->u.key;
        replay_put_dword(key->key->type);

        switch (key->key->type) {
        case KEY_VALUE_KIND_NUMBER:
            replay_put_qword(key->key->u.number);
            replay_put_byte(key->down);
            break;
        case KEY_VALUE_KIND_QCODE:
            replay_put_dword(key->key->u.qcode);
            replay_put_byte(key->down);
            break;
        case KEY_VALUE_KIND__MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = &evt->u.btn;
        replay_put_dword(btn->button);
        replay_put_byte(btn->down);
        break;
    case INPUT_EVENT_KIND_REL:
        move = &evt->u.rel;
        replay_put_dword(move->axis);
        replay_put_qword(move->value);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = &evt->u.abs;
        replay_put_dword(move->axis);
        replay_put_qword(move->value);
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }
}

InputEvent *replay_read_input_event(void)
{
    InputEvent evt;
    KeyValue keyValue;
    evt.u.key.key = &keyValue;

    evt.type = replay_get_dword();
    switch (evt.type) {
    case INPUT_EVENT_KIND_KEY:
        evt.u.key.key->type = replay_get_dword();

        switch (evt.u.key.key->type) {
        case KEY_VALUE_KIND_NUMBER:
            evt.u.key.key->u.number = replay_get_qword();
            evt.u.key.down = replay_get_byte();
            break;
        case KEY_VALUE_KIND_QCODE:
            evt.u.key.key->u.qcode = (QKeyCode)replay_get_dword();
            evt.u.key.down = replay_get_byte();
            break;
        case KEY_VALUE_KIND__MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        evt.u.btn.button = (InputButton)replay_get_dword();
        evt.u.btn.down = replay_get_byte();
        break;
    case INPUT_EVENT_KIND_REL:
        evt.u.rel.axis = (InputAxis)replay_get_dword();
        evt.u.rel.value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND_ABS:
        evt.u.abs.axis = (InputAxis)replay_get_dword();
        evt.u.abs.value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }

    return QAPI_CLONE(InputEvent, &evt);
}

void replay_input_event(QemuConsole *src, InputEvent *evt)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /* Nothing */
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_input_event(QAPI_CLONE(InputEvent, evt));
    } else {
        qemu_input_event_send_impl(src, evt);
    }
}

void replay_input_sync_event(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /* Nothing */
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_input_sync_event();
    } else {
        qemu_input_event_sync_impl();
    }
}
