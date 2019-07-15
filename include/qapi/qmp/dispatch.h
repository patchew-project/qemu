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

#ifndef QAPI_QMP_DISPATCH_H
#define QAPI_QMP_DISPATCH_H

#include "qemu/queue.h"
#include "qapi/qmp/json-parser.h"

typedef void (QmpCommandFunc)(QDict *, QObject **, Error **);

typedef enum QmpCommandOptions
{
    QCO_NO_OPTIONS            =  0x0,
    QCO_NO_SUCCESS_RESP       =  (1U << 0),
    QCO_ALLOW_OOB             =  (1U << 1),
    QCO_ALLOW_PRECONFIG       =  (1U << 2),
} QmpCommandOptions;

typedef struct QmpCommand
{
    const char *name;
    QmpCommandFunc *fn;
    QmpCommandOptions options;
    QTAILQ_ENTRY(QmpCommand) node;
    bool enabled;
} QmpCommand;

typedef QTAILQ_HEAD(QmpCommandList, QmpCommand) QmpCommandList;

typedef struct QmpSession QmpSession;
typedef void (QmpDispatchReturn) (QmpSession *session, QDict *rsp);

struct QmpSession {
    const QmpCommandList *cmds;
    JSONMessageParser parser;
    QmpDispatchReturn *return_cb;
};

void qmp_register_command(QmpCommandList *cmds, const char *name,
                          QmpCommandFunc *fn, QmpCommandOptions options);
const QmpCommand *qmp_find_command(const QmpCommandList *cmds,
                                   const char *name);
void qmp_session_init(QmpSession *session,
                      const QmpCommandList *cmds,
                      QmpDispatchReturn *return_cb);
static inline void
qmp_session_feed(QmpSession *session, const char *buf, size_t count)
{
    json_message_parser_feed(&session->parser, buf, count);
}
void qmp_session_destroy(QmpSession *session);
void qmp_disable_command(QmpCommandList *cmds, const char *name);
void qmp_enable_command(QmpCommandList *cmds, const char *name);

bool qmp_command_is_enabled(const QmpCommand *cmd);
const char *qmp_command_name(const QmpCommand *cmd);
bool qmp_has_success_response(const QmpCommand *cmd);
QDict *qmp_error_response(Error *err);
void qmp_dispatch(QmpSession *session, QObject *request,
                  bool allow_oob);
bool qmp_is_oob(const QDict *dict);

typedef void (*qmp_cmd_callback_fn)(const QmpCommand *cmd, void *opaque);

void qmp_for_each_command(const QmpCommandList *cmds, qmp_cmd_callback_fn fn,
                          void *opaque);

#endif
