/*
 * Text pretty printing Visitor
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Author: Daniel Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef TEXT_OUTPUT_VISITOR_H
#define TEXT_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct TextOutputVisitor TextOutputVisitor;

/**
 * text_output_visitor_new:
 * @extraIndent: number of extra levels of indent to apply
 * @skipLevel: skip output of nodes less than depth @skipLevel
 *
 * Create a new output visitor for displaying objects
 * in a pretty-printed text format. The @extraIdent
 * parameter can be used to add extra levels of whitespace
 * indentation on the output text. If there are some nodes
 * at the top level of the QAPI object that should not be
 * displayed, the @skipLevel parameter can be set to a
 * non-zero value to hide them.
 *
 * The objects are printed in a multi-line indented
 * fashion, such that each line contains a single
 * value. Extra indentation is added each time a
 * compound type (list, struct) is entered.
 *
 * To obtain the formatted string, call visit_complete()
 * passing a pointer to a "char *".
 *
 * <example>
 *   <title>Print of a complex type</title>
 *   <programlisting>
 *  name: hello
 *  num: 1729
 *  accounts:
 *      [0]:
 *          num: 1729
 *          name: hello
 *      [1]:
 *          num: 1729
 *          name: hello
 *      [2]:
 *          num: 1729
 *          name: hello
 *          info:
 *              help: world
 *      [3]:
 *          num: 1729
 *          name: hello
 *      [4]:
 *          num: 1729
 *          name: hello
 *          payments:
 *              [0]: 1729
 *              [1]: 1729
 *              [2]: 1729
 *   </programlisting>
 * </example>
 *
 * Returns: a pointer to new output visitor
 */
Visitor *text_output_visitor_new(int extraIndent,
                                 int skipLevel);


#endif
