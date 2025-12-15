/*
 * QAPI common helper functions
 *
 * This file provides helper functions related to types defined
 * in the QAPI schema.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qapi/qapi-types-common.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

HumanReadableText *human_readable_text_from_str(GString *str);

/*
 * Produce and return a NULL-terminated array of strings from @list.
 * The result is g_malloc()'d and all strings are g_strdup()'d.  It
 * can be freed with g_strfreev(), or by g_auto(GStrv) automatic
 * cleanup.
 */
char **strv_from_str_list(const strList *list);

/*
 * Merge @src over @dst by copying deep clones of the present members
 * from @src to @dst. Non-present on @src are left untouched on @dst.
 */
#define QAPI_MERGE(type, dst_, src_)                                    \
    ({                                                                  \
        QObject *out_ = NULL;                                           \
        Visitor *v_;                                                    \
        /* read in from src */                                          \
        v_ = qobject_output_visitor_new(&out_);                         \
        visit_type_ ## type(v_, NULL, &src_, &error_abort);             \
        visit_complete(v_, &out_);                                      \
        visit_free(v_);                                                 \
        /*                                                              \
         * Write to dst but leave existing fields intact (except for    \
         * has_* which will be updated according to their presence in   \
         * src).                                                        \
         */                                                             \
        v_ = qobject_input_visitor_new(out_);                           \
        visit_start_struct(v_, NULL, NULL, 0, &error_abort);            \
        visit_type_ ## type ## _members(v_, dst_, &error_abort);        \
        visit_check_struct(v_, &error_abort);                           \
        visit_end_struct(v_, NULL);                                     \
        visit_free(v_);                                                 \
        qobject_unref(out_);                                            \
    })
