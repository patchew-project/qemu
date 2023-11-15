/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - serialize non-volatile varstore from/to json,
 *                    using qapi
 *
 * tools which can read/write these json files:
 *  - https://gitlab.com/kraxel/virt-firmware
 *  - https://github.com/awslabs/python-uefivars
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "sysemu/dma.h"

#include "hw/uefi/var-service.h"

#include "qapi/dealloc-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qapi-types-uefi.h"
#include "qapi/qapi-visit-uefi.h"

static UefiVarStore *uefi_vars_to_qapi(uefi_vars_state *uv)
{
    static const char hex[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    UefiVarStore *vs;
    UefiVariableList **tail;
    UefiVariable *v;
    QemuUUID be;
    uefi_variable *var;
    uint8_t *data;
    unsigned int i;

    vs = g_new0(UefiVarStore, 1);
    vs->version = 2;
    tail = &vs->variables;

    QTAILQ_FOREACH(var, &uv->variables, next) {
        if (!(var->attributes & EFI_VARIABLE_NON_VOLATILE)) {
            continue;
        }

        v = g_new0(UefiVariable, 1);
        be = qemu_uuid_bswap(var->guid);
        v->guid = qemu_uuid_unparse_strdup(&be);
        v->name = uefi_ucs2_to_ascii(var->name, var->name_size);
        v->attr = var->attributes;

        v->data = g_malloc(var->data_size * 2 + 1);
        data = var->data;
        for (i = 0; i < var->data_size * 2;) {
            v->data[i++] = hex[*data >> 4];
            v->data[i++] = hex[*data & 15];
            data++;
        }
        v->data[i++] = 0;

        QAPI_LIST_APPEND(tail, v);
    }
    return vs;
}

static unsigned parse_hexchar(char c)
{
    switch (c) {
    case '0' ... '9': return c - '0';
    case 'a' ... 'f': return c - 'a' + 0xa;
    case 'A' ... 'F': return c - 'A' + 0xA;
    default: return 0;
    }
}

static void uefi_vars_from_qapi(uefi_vars_state *uv, UefiVarStore *vs)
{
    UefiVariableList *item;
    UefiVariable *v;
    QemuUUID be;
    uefi_variable *var;
    uint8_t *data;
    size_t i, len;

    for (item = vs->variables; item != NULL; item = item->next) {
        v = item->value;

        var = g_new0(uefi_variable, 1);
        var->attributes = v->attr;
        qemu_uuid_parse(v->guid, &be);
        var->guid = qemu_uuid_bswap(be);

        len = strlen(v->name);
        var->name_size = len * 2 + 2;
        var->name = g_malloc(var->name_size);
        for (i = 0; i <= len; i++) {
            var->name[i] = v->name[i];
        }

        len = strlen(v->data);
        var->data_size = len / 2;
        var->data = data = g_malloc(var->data_size);
        for (i = 0; i < len; i += 2) {
            *(data++) =
                parse_hexchar(v->data[i]) << 4 |
                parse_hexchar(v->data[i + 1]);
        }

        QTAILQ_INSERT_TAIL(&uv->variables, var, next);
    }
}

static GString *uefi_vars_to_json(uefi_vars_state *uv)
{
    UefiVarStore *vs = uefi_vars_to_qapi(uv);
    QObject *qobj = NULL;
    Visitor *v;
    GString *gstr;

    v = qobject_output_visitor_new(&qobj);
    if (visit_type_UefiVarStore(v, NULL, &vs, NULL)) {
        visit_complete(v, &qobj);
    }
    visit_free(v);
    qapi_free_UefiVarStore(vs);

    gstr = qobject_to_json_pretty(qobj, true);
    qobject_unref(qobj);

    return gstr;
}

void uefi_vars_json_init(uefi_vars_state *uv, Error **errp)
{
    if (uv->jsonfile) {
        uv->jsonfd = qemu_create(uv->jsonfile, O_RDWR, 0666, errp);
    }
}

void uefi_vars_json_save(uefi_vars_state *uv)
{
    GString *gstr;

    if (uv->jsonfd == -1) {
        return;
    }

    gstr = uefi_vars_to_json(uv);

    lseek(uv->jsonfd, 0, SEEK_SET);
    write(uv->jsonfd, gstr->str, gstr->len);
    ftruncate(uv->jsonfd, gstr->len);
    fsync(uv->jsonfd);

    g_string_free(gstr, true);
}

void uefi_vars_json_load(uefi_vars_state *uv, Error **errp)
{
    UefiVarStore *vs;
    QObject *qobj;
    Visitor *v;
    char *str;
    size_t len;

    if (uv->jsonfd == -1) {
        return;
    }

    len = lseek(uv->jsonfd, 0, SEEK_END);
    if (len == 0) {
        return;
    }

    str = g_malloc(len + 1);
    lseek(uv->jsonfd, 0, SEEK_SET);
    read(uv->jsonfd, str, len);
    str[len] = 0;

    qobj = qobject_from_json(str, errp);
    v = qobject_input_visitor_new(qobj);
    visit_type_UefiVarStore(v, NULL, &vs, errp);
    visit_free(v);

    if (!(*errp)) {
        uefi_vars_from_qapi(uv, vs);
    }

    qapi_free_UefiVarStore(vs);
    qobject_unref(qobj);
    g_free(str);
}
