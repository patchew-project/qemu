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

#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qmp/qbool.h"

Visitor *qobject_input_visitor_new_qmp(QObject *obj)
{
    Visitor *v = qobject_input_visitor_new(obj);

    visit_set_policy(v, &compat_policy);
    return v;
}

Visitor *qobject_output_visitor_new_qmp(QObject **result)
{
    Visitor *v = qobject_output_visitor_new(result);

    visit_set_policy(v, &compat_policy);
    return v;
}

static QDict *qmp_dispatch_check_obj(QDict *dict, bool allow_oob,
                                     Error **errp)
{
    const char *exec_key = NULL;
    const QDictEntry *ent;
    const char *arg_name;
    const QObject *arg_obj;

    for (ent = qdict_first(dict); ent;
         ent = qdict_next(dict, ent)) {
        arg_name = qdict_entry_key(ent);
        arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")
            || (!strcmp(arg_name, "exec-oob") && allow_oob)) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, "QMP input member '%s' must be a string",
                           arg_name);
                return NULL;
            }
            if (exec_key) {
                error_setg(errp, "QMP input member '%s' clashes with '%s'",
                           arg_name, exec_key);
                return NULL;
            }
            exec_key = arg_name;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp,
                           "QMP input member 'arguments' must be an object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            continue;
        } else {
            error_setg(errp, "QMP input member '%s' is unexpected",
                       arg_name);
            return NULL;
        }
    }

    if (!exec_key) {
        error_setg(errp, "QMP input lacks member 'execute'");
        return NULL;
    }

    return dict;
}

QDict *qmp_error_response(Error *err)
{
    QDict *rsp;

    rsp = qdict_from_jsonf_nofail("{ 'error': { 'class': %s, 'desc': %s } }",
                                  QapiErrorClass_str(error_get_class(err)),
                                  error_get_pretty(err));
    error_free(err);
    return rsp;
}

/*
 * Does @qdict look like a command to be run out-of-band?
 */
bool qmp_is_oob(const QDict *dict)
{
    return qdict_haskey(dict, "exec-oob")
        && !qdict_haskey(dict, "execute");
}

QDict *qmp_dispatch(const QmpCommandList *cmds, QObject *request,
                    bool allow_oob, void *exec_data)
{
    Error *err = NULL;
    bool oob;
    const char *command;
    QDict *args;
    const QmpCommand *cmd;
    QDict *dict;
    QObject *id;
    QObject *ret = NULL;
    QDict *rsp = NULL;

    dict = qobject_to(QDict, request);
    if (!dict) {
        id = NULL;
        error_setg(&err, "QMP input must be a JSON object");
        goto out;
    }

    id = qdict_get(dict, "id");

    if (!qmp_dispatch_check_obj(dict, allow_oob, &err)) {
        goto out;
    }

    command = qdict_get_try_str(dict, "execute");
    oob = false;
    if (!command) {
        assert(allow_oob);
        command = qdict_get_str(dict, "exec-oob");
        oob = true;
    }
    cmd = qmp_find_command(cmds, command);
    if (cmd == NULL) {
        error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", command);
        goto out;
    }
    if (!compat_policy_input_ok(cmd->special_features, &compat_policy,
                                ERROR_CLASS_COMMAND_NOT_FOUND,
                                "command", command, &err)) {
        goto out;
    }
    if (!cmd->enabled) {
        error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Command %s has been disabled%s%s",
                  command,
                  cmd->disable_reason ? ": " : "",
                  cmd->disable_reason ?: "");
        goto out;
    }
    if (oob && !(cmd->options & QCO_ALLOW_OOB)) {
        error_setg(&err, "The command %s does not support OOB",
                   command);
        goto out;
    }

    if (!qmp_command_available(cmd, &err)) {
        goto out;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
        qobject_ref(args);
    }

    qmp_dispatch_exec(cmd, oob, exec_data, args, &ret, &err);

    qobject_unref(args);
    if (err) {
        /* or assert(!ret) after reviewing all handlers: */
        qobject_unref(ret);
        goto out;
    }

    if (cmd->options & QCO_NO_SUCCESS_RESP) {
        g_assert(!ret);
        return NULL;
    } else if (!ret) {
        /*
         * When the command's schema has no 'returns', cmd->fn()
         * leaves @ret null.  The QMP spec calls for an empty object
         * then; supply it.
         */
        ret = QOBJECT(qdict_new());
    }

    rsp = qdict_new();
    qdict_put_obj(rsp, "return", ret);

out:
    if (err) {
        assert(!rsp);
        rsp = qmp_error_response(err);
    }

    assert(rsp);

    if (id) {
        qdict_put_obj(rsp, "id", qobject_ref(id));
    }

    return rsp;
}
