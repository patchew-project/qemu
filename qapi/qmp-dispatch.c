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
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"

QmpReturn *qmp_return_new(QmpSession *session, const QDict *req)
{
    QmpReturn *qret = g_new0(QmpReturn, 1);
    QObject *id = req ? qdict_get(req, "id") : NULL;

    qret->oob = req ? qmp_is_oob(req) : false;
    qret->session = session;
    qret->rsp = qdict_new();
    if (id) {
        qobject_incref(id);
        qdict_put_obj(qret->rsp, "id", id);
    }

    qemu_mutex_lock(&session->pending_lock);
    QTAILQ_INSERT_TAIL(&session->pending, qret, entry);
    qemu_mutex_unlock(&session->pending_lock);

    return qret;
}

static void qmp_return_free_with_lock(QmpReturn *qret)
{
    if (qret->session) {
        QTAILQ_REMOVE(&qret->session->pending, qret, entry);
    }
    QDECREF(qret->rsp);
    g_free(qret);
}

void qmp_return_free(QmpReturn *qret)
{
    QmpSession *session = qret->session;

    if (session) {
        qemu_mutex_lock(&session->pending_lock);
    }

    qmp_return_free_with_lock(qret);

    if (session) {
        qemu_mutex_unlock(&session->pending_lock);
    }
}

static void qmp_return_orderly(QmpReturn *qret)
{
    QmpSession *session = qret->session;
    QmpReturn *ret, *next;

    if (!session) {
        /* the client was destroyed before return, discard */
        qmp_return_free(qret);
        return;
    }
    if (qret->oob) {
        session->return_cb(session, qret->rsp);
        qmp_return_free(qret);
        return;
    }

    /* mark as finished */
    qret->session = NULL;

    qemu_mutex_lock(&session->pending_lock);
    /* process the list of pending and return until reaching an unfinshed */
    QTAILQ_FOREACH_SAFE(ret, &session->pending, entry, next) {
        if (ret->session) {
            goto end;
        }
        session->return_cb(session, ret->rsp);
        ret->session = session;
        qmp_return_free_with_lock(ret);
    }
end:
    qemu_mutex_unlock(&session->pending_lock);
}

void qmp_return(QmpReturn *qret, QObject *rsp)
{
    qdict_put_obj(qret->rsp, "return", rsp ?: QOBJECT(qdict_new()));
    qmp_return_orderly(qret);
}

void qmp_return_error(QmpReturn *qret, Error *err)
{
    QDict *qdict = qdict_new();

    qdict_put_str(qdict, "class", QapiErrorClass_str(error_get_class(err)));
    qdict_put_str(qdict, "desc", error_get_pretty(err));
    qdict_put_obj(qret->rsp, "error", QOBJECT(qdict));
    error_free(err);
    qmp_return_orderly(qret);
}

QDict *qmp_dispatch_check_obj(const QObject *request, Error **errp)
{
    const QDictEntry *ent;
    const char *arg_name;
    const QObject *arg_obj;
    bool has_exec_key = false;
    QDict *dict = NULL;

    dict = qobject_to(QDict, request);
    if (!dict) {
        error_setg(errp, "QMP input must be a JSON object");
        return NULL;
    }

    for (ent = qdict_first(dict); ent;
         ent = qdict_next(dict, ent)) {
        arg_name = qdict_entry_key(ent);
        arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp,
                           "QMP input member 'execute' must be a string");
                return NULL;
            }
            has_exec_key = true;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp,
                           "QMP input member 'arguments' must be an object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            continue;
        } else if (!strcmp(arg_name, "control")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp,
                           "QMP input member 'control' must be a dict");
                return NULL;
            }
        } else {
            error_setg(errp, "QMP input member '%s' is unexpected",
                       arg_name);
            return NULL;
        }
    }

    if (!has_exec_key) {
        error_setg(errp, "QMP input lacks member 'execute'");
        return NULL;
    }

    return dict;
}

void qmp_dispatch(QmpSession *session, QDict *req)
{
    const char *command;
    QDict *args = NULL;
    QmpCommand *cmd;
    Error *err = NULL;

    command = qdict_get_str(req, "execute");
    cmd = qmp_find_command(session->cmds, command);
    if (cmd == NULL) {
        error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", command);
        goto end;
    }
    if (!cmd->enabled) {
        error_setg(&err, "The command %s has been disabled for this instance",
                   command);
        goto end;
    }

    if (!qdict_haskey(req, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(req, "arguments");
        QINCREF(args);
    }

    {
        QObject *ret = NULL;
        cmd->fn(args, &ret, &err);
        if (err || cmd->options & QCO_NO_SUCCESS_RESP) {
            assert(!ret);
            goto end;
        } else if (!ret) {
            ret = QOBJECT(qdict_new());
        }
        qmp_return(qmp_return_new(session, req), ret);
    }

end:
    if (err) {
        qmp_return_error(qmp_return_new(session, req), err);
    }
    QDECREF(args);
}

/*
 * Detect whether a request should be run out-of-band, by quickly
 * peeking at whether we have: { "control": { "run-oob": true } }. By
 * default commands are run in-band.
 */
bool qmp_is_oob(const QDict *dict)
{
    QBool *bool_obj;

    dict = qdict_get_qdict(dict, "control");
    if (!dict) {
        return false;
    }

    bool_obj = qobject_to(QBool, qdict_get(dict, "run-oob"));
    if (!bool_obj) {
        return false;
    }

    return qbool_get_bool(bool_obj);
}

static void qmp_json_parser_emit(JSONMessageParser *parser, GQueue *tokens)
{
    QmpSession *session = container_of(parser, QmpSession, parser);
    QObject *obj;
    QDict *req = NULL;
    Error *err = NULL;

    obj = json_parser_parse_err(tokens, NULL, &err);
    if (err) {
        goto end;
    }

    req = qmp_dispatch_check_obj(obj, &err);
    if (err) {
        goto end;
    }

    session->dispatch_cb(session, req);

end:
    if (err) {
        qmp_return_error(qmp_return_new(session, req), err);
    }
    qobject_decref(obj);
}

void qmp_session_init(QmpSession *session,
                      QmpCommandList *cmds,
                      QmpDispatch *dispatch_cb,
                      QmpDispatchReturn *return_cb)
{
    assert(return_cb);
    assert(!session->return_cb);

    json_message_parser_init(&session->parser, qmp_json_parser_emit);
    session->cmds = cmds;
    session->dispatch_cb = dispatch_cb;
    session->return_cb = return_cb;
    qemu_mutex_init(&session->pending_lock);
    QTAILQ_INIT(&session->pending);
}

void qmp_session_destroy(QmpSession *session)
{
    QmpReturn *ret, *next;

    if (!session->return_cb) {
        return;
    }

    qemu_mutex_lock(&session->pending_lock);
    QTAILQ_FOREACH_SAFE(ret, &session->pending, entry, next) {
        ret->session = NULL;
        QTAILQ_REMOVE(&session->pending, ret, entry);
    }
    qemu_mutex_unlock(&session->pending_lock);
    session->cmds = NULL;
    session->dispatch_cb = NULL;
    session->return_cb = NULL;
    json_message_parser_destroy(&session->parser);
    qemu_mutex_destroy(&session->pending_lock);
}
