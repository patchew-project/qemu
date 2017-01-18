/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qjson.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"

void qmp_client_feed(QmpClient *client,
                     const char *buffer, size_t size)
{
    json_message_parser_feed(&client->parser, buffer, size);
}

static QDict *qmp_dispatch_check_obj(const QObject *request, Error **errp)
{
    const QDictEntry *ent;
    const char *arg_name;
    const QObject *arg_obj;
    bool has_exec_key = false;
    QDict *dict = NULL;

    if (qobject_type(request) != QTYPE_QDICT) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT,
                   "request is not a dictionary");
        return NULL;
    }

    dict = qobject_to_qdict(request);

    for (ent = qdict_first(dict); ent;
         ent = qdict_next(dict, ent)) {
        arg_name = qdict_entry_key(ent);
        arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER, "execute",
                           "string");
                return NULL;
            }
            has_exec_key = true;
        } else if (!strcmp(arg_name, "id")) {
            /* top-level 'id' is accepted */
        } else if (strcmp(arg_name, "arguments")) {
            error_setg(errp, QERR_QMP_EXTRA_MEMBER, arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "execute");
        return NULL;
    }

    return dict;
}

static QObject *do_qmp_dispatch(QObject *request, QmpReturn *qret, Error **errp)
{
    Error *local_err = NULL;
    const char *command;
    QDict *args, *dict;
    QmpCommand *cmd;
    QObject *ret = NULL;

    dict = qmp_dispatch_check_obj(request, errp);
    if (!dict) {
        return NULL;
    }

    command = qdict_get_str(dict, "execute");
    cmd = qmp_find_command(command);
    if (cmd == NULL) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", command);
        return NULL;
    }
    if (!cmd->enabled) {
        error_setg(errp, "The command %s has been disabled for this instance",
                   command);
        return NULL;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
        QINCREF(args);
    }

    switch (cmd->type) {
    case QCT_NORMAL:
        cmd->fn(args, &ret, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        } else if (cmd->options & QCO_NO_SUCCESS_RESP) {
            g_assert(!ret);
        } else if (!ret) {
            ret = QOBJECT(qdict_new());
        }
        break;
    case QCT_ASYNC:
        if (qret->client->has_async &&
            !qdict_haskey(qret->rsp, "id")) {
            error_setg(errp, "An async command requires an 'id'");
            break;
        }

        cmd->fn_async(args, qret);
        break;
    }

    QDECREF(args);

    return ret;
}

QObject *qmp_build_error_object(Error *err)
{
    return qobject_from_jsonf("{ 'class': %s, 'desc': %s }",
                              QapiErrorClass_lookup[error_get_class(err)],
                              error_get_pretty(err));
}

static void qmp_return_free(QmpReturn *qret)
{
    QDict *rsp = qret->rsp;

    if (qret->client) {
        QLIST_REMOVE(qret, link);
    }

    qobject_decref(QOBJECT(rsp));
    g_free(qret);
}

static void do_qmp_return(QmpReturn *qret)
{
    QDict *rsp = qret->rsp;
    QmpClient *client = qret->client;

    if (client) {
        client->return_cb(client, QOBJECT(rsp));
    }

    qmp_return_free(qret);
}

void qmp_return(QmpReturn *qret, QObject *cmd_rsp)
{
    qdict_put_obj(qret->rsp, "return", cmd_rsp ?: QOBJECT(qdict_new()));

    do_qmp_return(qret);
}

void qmp_return_error(QmpReturn *qret, Error *err)
{
    qdict_put_obj(qret->rsp, "error", qmp_build_error_object(err));
    error_free(err);

    do_qmp_return(qret);
}

bool qmp_return_is_cancelled(QmpReturn *qret)
{
    if (!qret->client) {
        qmp_return_free(qret);
        return true;
    }

    return false;
}

/*
 * Input object checking rules
 *
 * 1. Input object must be a dict
 * 2. The "execute" key must exist
 * 3. The "execute" key must be a string
 * 4. If the "arguments" key exists, it must be a dict
 * 5. If the "id" key exists, it can be anything (ie. json-value)
 * 6. Any argument not listed above is considered invalid
 */
static QDict *qmp_check_input_obj(QObject *input_obj, Error **errp)
{
    const QDictEntry *ent;
    int has_exec_key = 0;
    QDict *dict;

    if (qobject_type(input_obj) != QTYPE_QDICT) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "object");
        return NULL;
    }

    dict = qobject_to_qdict(input_obj);

    for (ent = qdict_first(dict); ent; ent = qdict_next(dict, ent)) {
        const char *arg_name = qdict_entry_key(ent);
        const QObject *arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
                           "execute", "string");
                return NULL;
            }
            has_exec_key = 1;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
                           "arguments", "object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            /* Any string is acceptable as "id", so nothing to check */
        } else {
            error_setg(errp, QERR_QMP_EXTRA_MEMBER, arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        error_setg(errp, QERR_QMP_BAD_INPUT_OBJECT, "execute");
        return NULL;
    }

    return dict;
}

static void handle_qmp_command(JSONMessageParser *parser, GQueue *tokens)
{
    QmpClient *client = container_of(parser, QmpClient, parser);
    QObject *req, *id = NULL;
    QDict *qdict, *rqdict = qdict_new();
    Error *err = NULL;

    req = json_parser_parse_err(tokens, NULL, &err);
    if (err || !req || qobject_type(req) != QTYPE_QDICT) {
        if (!err) {
            error_setg(&err, QERR_JSON_PARSING);
        }
        goto err_out;
    }

    qdict = qmp_check_input_obj(req, &err);
    if (!qdict) {
        goto err_out;
    }

    id = qdict_get(qdict, "id");
    if (id) {
        qobject_incref(id);
        qdict_del(qdict, "id");
        qdict_put_obj(rqdict, "id", id);
    }

    if (client->pre_dispatch_cb &&
        !client->pre_dispatch_cb(client, QOBJECT(qdict), &err)) {
        goto err_out;
    }

    qmp_dispatch(client, req, rqdict);

    if (client->post_dispatch_cb &&
        !client->post_dispatch_cb(client, QOBJECT(qdict), &err)) {
        goto err_out;
    }

    qobject_decref(req);
    return;

err_out:
    if (err) {
        qdict_put_obj(rqdict, "error", qmp_build_error_object(err));
        error_free(err);
        client->return_cb(client, QOBJECT(rqdict));
    }

    QDECREF(rqdict);
    qobject_decref(req);
}

void qmp_client_init(QmpClient *client,
                     QmpPreDispatch *pre_dispatch_cb,
                     QmpPostDispatch *post_dispatch_cb,
                     QmpDispatchReturn *return_cb)
{
    assert(!client->return_cb);

    json_message_parser_init(&client->parser, handle_qmp_command);
    client->pre_dispatch_cb = pre_dispatch_cb;
    client->post_dispatch_cb = post_dispatch_cb;
    client->return_cb = return_cb;
    QLIST_INIT(&client->pending);
}

void qmp_client_destroy(QmpClient *client)
{
    QmpReturn *ret, *next;

    client->return_cb = NULL;
    json_message_parser_destroy(&client->parser);
    /* Remove the weak references to the pending returns. The
     * dispatched function is the owner of QmpReturn, and will have to
     * qmp_return(). (it might be interesting to have a way to notify
     * that the client disconnected to cancel an on-going
     * operation) */
    QLIST_FOREACH_SAFE(ret, &client->pending, link, next) {
        ret->client = NULL;
        QLIST_REMOVE(ret, link);
    }
}

void qmp_dispatch(QmpClient *client, QObject *request, QDict *rsp)
{
    Error *err = NULL;
    QmpReturn *qret = g_new0(QmpReturn, 1);
    QObject *ret, *id;
    QDict *req;

    assert(client);

    qret->rsp = rsp ?: qdict_new();
    qret->client = client;
    QLIST_INSERT_HEAD(&client->pending, qret, link);

    req = qobject_to_qdict(request);
    id = qdict_get(req, "id");
    if (id) {
        qobject_incref(id);
        qdict_put_obj(qret->rsp, "id", id);
    }

    ret = do_qmp_dispatch(request, qret, &err);

    if (err) {
        assert(!ret);
        qmp_return_error(qret, err);
        return;
    } else if (ret) {
        qmp_return(qret, ret);
        return;
    }
}
