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
#include "qemu/thread.h"

typedef struct QmpReturn QmpReturn;

typedef void (QmpCommandFunc)(QDict *, QObject **, Error **);
typedef void (QmpCommandAsyncFunc)(QDict *, QmpReturn *);

typedef enum QmpCommandOptions
{
    QCO_NO_OPTIONS            =  0x0,
    QCO_NO_SUCCESS_RESP       =  (1U << 0),
    QCO_ALLOW_OOB             =  (1U << 1),
    QCO_ALLOW_PRECONFIG       =  (1U << 2),
    QCO_ASYNC                 =  (1U << 3),
} QmpCommandOptions;

typedef struct QmpCommand
{
    const char *name;
    union {
        QmpCommandFunc *fn;
        QmpCommandAsyncFunc *async_fn;
    };
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
    QemuMutex pending_lock;
    QTAILQ_HEAD(, QmpReturn) pending;
};

struct QmpReturn {
    QmpSession *session;
    QDict *rsp;
    bool oob;
    bool finished;
    QTAILQ_ENTRY(QmpReturn) entry;
};

/**
 * qmp_return_new:
 *
 * Allocates and initializes a QmpReturn.
 */
QmpReturn *qmp_return_new(QmpSession *session, const QObject *req);

/**
 * qmp_return_free:
 *
 * Free a QmpReturn. This shouldn't be needed if you actually return
 * with qmp_return{_error}.
 */
void qmp_return_free(QmpReturn *qret);

/**
 * qmp_return{_error}:
 *
 * Construct the command reply, and call the
 * return_cb() associated with the session.
 *
 * Finally, free the QmpReturn.
 */
void qmp_return(QmpReturn *qret, QObject *rsp);
void qmp_return_error(QmpReturn *qret, Error *err);

/*
 * @qmp_return_is_cancelled:
 *
 * Return true if the QmpReturn is cancelled, and free the QmpReturn
 * in this case.
 */
bool qmp_return_is_cancelled(QmpReturn *qret);

void qmp_register_command(QmpCommandList *cmds, const char *name,
                          QmpCommandFunc *fn, QmpCommandOptions options);
void qmp_register_async_command(QmpCommandList *cmds, const char *name,
                                QmpCommandAsyncFunc *fn,
                                QmpCommandOptions options);
const QmpCommand *qmp_find_command(const QmpCommandList *cmds,
                                   const char *name);
void qmp_session_init(QmpSession *session,
                      const QmpCommandList *cmds,
                      JSONMessageEmit *emit,
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
void qmp_dispatch(QmpSession *session, QObject *request,
                  bool allow_oob);
bool qmp_is_oob(const QDict *dict);

typedef void (*qmp_cmd_callback_fn)(const QmpCommand *cmd, void *opaque);

void qmp_for_each_command(const QmpCommandList *cmds, qmp_cmd_callback_fn fn,
                          void *opaque);

#endif
