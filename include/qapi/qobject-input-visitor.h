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
 * qobject_input_visitor_new:
 * @obj: the input object to visit
 * @strict: whether to require that all input keys are consumed
 *
 * Create a new input visitor that converts a QObject to a QAPI object.
 *
 * Any scalar values in the @obj input data structure should be in the
 * required type already. i.e. if visiting a bool, the value should
 * already be a QBool instance.
 *
 * If @strict is set to true, then an error will be reported if any
 * dict keys are not consumed during visitation.
 *
 * The returned input visitor should be released by calling
 * visit_free() when no longer required.
 *
 * Returns: a new input visitor
 */
Visitor *qobject_input_visitor_new(QObject *obj, bool strict);

/**
 * qobject_string_input_visitor_new:
 * @obj: the input object to visit
 *
 * Create a new input visitor that converts a QObject to a QAPI object.
 *
 * Any scalar values in the @obj input data structure should always be
 * represented as strings. i.e. if visiting a boolean, the value should
 * be a QString whose contents represent a valid boolean.
 *
 * The visitor always operates in strict mode, requiring all dict keys
 * to be consumed during visitation.
 *
 * The returned input visitor should be released by calling
 * visit_free() when no longer required.
 *
 * Returns: a new input visitor
 */
Visitor *qobject_string_input_visitor_new(QObject *obj);

#endif
