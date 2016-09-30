/*
 * Input Visitor
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

#ifndef QOBJECT_INPUT_VISITOR_H
#define QOBJECT_INPUT_VISITOR_H

#include "qapi/visitor.h"
#include "qapi/qmp/qobject.h"

typedef struct QObjectInputVisitor QObjectInputVisitor;

/**
 * Create a new input visitor that converts @obj to a QAPI object.
 *
 * Any scalar values in the @obj input data structure should be in the
 * required type already. i.e. if visiting a bool, the value should
 * already be a QBool instance.
 *
 * If @strict is set to true, then an error will be reported if any
 * dict keys are not consumed during visitation. If @strict is false
 * then extra dict keys are silently ignored.
 *
 * The returned input visitor should be released by calling
 * visit_free() when no longer required.
 */
Visitor *qobject_input_visitor_new(QObject *obj, bool strict);

/**
 * Create a new input visitor that converts @obj to a QAPI object.
 *
 * Any scalar values in the @obj input data structure should always be
 * represented as strings. i.e. if visiting a boolean, the value should
 * be a QString whose contents represent a valid boolean.
 *
 * If @autocreate_list is true, then as an alternative to a normal QList,
 * list values can be stored as a QString or QDict instead, which will
 * be interpreted as representing single element lists. This should only
 * by used if compatibility is required with the OptsVisitor which allowed
 * repeated keys, without list indexes, to represent lists. e.g. set this
 * to true if you have compatibility requirements for
 *
 *   -arg foo=hello,foo=world
 *
 * to be treated as equivalent to the preferred syntax:
 *
 *   -arg foo.0=hello,foo.1=world
 *
 * The visitor always operates in strict mode, requiring all dict keys
 * to be consumed during visitation. An error will be reported if this
 * does not happen.
 *
 * The returned input visitor should be released by calling
 * visit_free() when no longer required.
 */
Visitor *qobject_input_visitor_new_autocast(QObject *obj,
                                            bool autocreate_list);


/**
 * Create a new input visitor that converts @opts to a QAPI object.
 *
 * The QemuOpts will be converted into a QObject using the
 * qdict_crumple() method to automatically create structs
 * and lists. The resulting QDict will then be passed to the
 * qobject_input_visitor_new_autocast() method. See the docs
 * of that method for further details on processing behaviour.
 *
 * The returned input visitor should be released by calling
 * visit_free() when no longer required.
 */
Visitor *qobject_input_visitor_new_opts(const QemuOpts *opts,
                                        bool autocreate_list,
                                        Error **errp);

#endif
