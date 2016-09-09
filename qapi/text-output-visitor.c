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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu-common.h"
#include "qapi/text-output-visitor.h"
#include "qapi/visitor-impl.h"
#include <math.h>

struct TextOutputVisitorState {
    bool isList;
    size_t listIndex;

    QSIMPLEQ_ENTRY(TextOutputVisitorState) next;
};

struct TextOutputVisitor {
    Visitor visitor;
    GString *string;
    int level;
    int skipLevel;
    int extraIndent;

    QSIMPLEQ_HEAD(TextOutputVisitorStateHead, TextOutputVisitorState) state;
};

#define INDENT (tov->extraIndent + ((tov->level - tov->skipLevel) * 4))

static TextOutputVisitor *
text_output_visitor_from_visitor(Visitor *v)
{
    return container_of(v, TextOutputVisitor, visitor);
}


static void
text_output_visitor_open_compound_type(struct TextOutputVisitor *tov,
                                       bool isList)
{
    struct TextOutputVisitorState *currstate = QSIMPLEQ_FIRST(&tov->state);
    struct TextOutputVisitorState *state =
        g_new0(struct TextOutputVisitorState, 1);

    if (currstate && currstate->isList) {
        g_string_append_printf(tov->string, "\n");
    }
    state->isList = isList;

    QSIMPLEQ_INSERT_HEAD(&tov->state, state, next);

    tov->level++;
}


static void
text_output_visitor_close_compound_type(struct TextOutputVisitor *tov)
{
    struct TextOutputVisitorState *state = QSIMPLEQ_FIRST(&tov->state);

    tov->level--;

    QSIMPLEQ_REMOVE_HEAD(&tov->state, next);

    g_free(state);
}


static void
text_output_visitor_print_list_index(struct TextOutputVisitor *tov)
{
    struct TextOutputVisitorState *state = QSIMPLEQ_FIRST(&tov->state);

    if (!state || !state->isList) {
        return;
    }

    if (tov->level >= tov->skipLevel) {
        g_string_append_printf(tov->string,
                               "%*s[%zu]:",
                               INDENT, "", state->listIndex++);
    }
}


static char *
text_output_visitor_format_name(const char *name)
{
    if (!name) {
        return g_strdup("<anon>");
    } else {
        char *ret = g_strdup(name);
        gsize i;
        for (i = 0; ret[i]; i++) {
            if (ret[i] == '-') {
                ret[i] = ' ';
            }
        }
        return ret;
    }
}

static void
text_output_visitor_print_scalar(TextOutputVisitor *tov,
                                 const char *name,
                                 const char *fmt,
                                 ...)
{
    struct TextOutputVisitorState *state = QSIMPLEQ_FIRST(&tov->state);
    va_list vargs;
    char *val;
    char *key;

    va_start(vargs, fmt);

    text_output_visitor_print_list_index(tov);

    val = g_strdup_vprintf(fmt, vargs);

    if ((!state && !name) ||
        (state && state->isList)) {
        g_string_append_printf(tov->string,
                               !state ? "%s\n" : " %s\n", val);
    } else {
        key = text_output_visitor_format_name(name);
        g_string_append_printf(tov->string,
                               "%*s%s: %s\n",
                               INDENT, "", key, val);
        g_free(key);
    }

    g_free(val);
    va_end(vargs);
}


static void
text_output_visitor_print_type_int64(Visitor *v, const char *name, int64_t *obj,
                                     Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->level < tov->skipLevel) {
        return;
    }

    text_output_visitor_print_scalar(tov, name, "%" PRIu64, *obj);
}


static void
text_output_visitor_print_type_uint64(Visitor *v, const char *name,
                                      uint64_t *obj, Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->level < tov->skipLevel) {
        return;
    }

    text_output_visitor_print_scalar(tov, name, "%" PRIi64, *obj);
}


static void
text_output_visitor_print_type_size(Visitor *v, const char *name, uint64_t *obj,
                                    Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);
    char *szval;

    if (tov->level < tov->skipLevel) {
        return;
    }

    szval = qemu_szutostr_full(*obj, '\0', true, " ");
    text_output_visitor_print_scalar(tov, name, "%" PRIu64 " (%s)",
                                     *obj, szval);
    g_free(szval);
}


static void
text_output_visitor_print_type_bool(Visitor *v, const char *name, bool *obj,
                                    Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->level < tov->skipLevel) {
        return;
    }

    text_output_visitor_print_scalar(tov, name, "%s", *obj ? "true" : "false");
}


static void
text_output_visitor_print_type_str(Visitor *v, const char *name, char **obj,
                                   Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->level < tov->skipLevel) {
        return;
    }

    text_output_visitor_print_scalar(tov, name, "%s", *obj ? *obj : "<null>");
}


static void
text_output_visitor_print_type_number(Visitor *v, const char *name, double *obj,
                                      Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->level < tov->skipLevel) {
        return;
    }

    text_output_visitor_print_scalar(tov, name, "%f", *obj);
}


static void
text_output_visitor_start_list(Visitor *v, const char *name, GenericList **list,
                               size_t size, Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);
    char *key;

    if (tov->level >= tov->skipLevel && name) {
        key = text_output_visitor_format_name(name);
        g_string_append_printf(tov->string,
                               "%*s%s:\n",
                               INDENT, "",
                               key);
        g_free(key);
    }
    text_output_visitor_open_compound_type(tov, true);
}


static GenericList *
text_output_visitor_next_list(Visitor *v, GenericList *tail, size_t size)
{
    GenericList *ret = tail->next;
    return ret;
}


static void
text_output_visitor_end_list(Visitor *v, void **obj)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    text_output_visitor_close_compound_type(tov);
}


static void
text_output_visitor_start_struct(Visitor *v, const char *name, void **obj,
                                 size_t size, Error **errp)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);
    struct TextOutputVisitorState *state = QSIMPLEQ_FIRST(&tov->state);

    if (tov->level >= tov->skipLevel && name &&
        state && !state->isList) {
        g_string_append_printf(tov->string,
                               "%*s%s:\n",
                               INDENT, "",
                               name);
    }
    text_output_visitor_print_list_index(tov);
    text_output_visitor_open_compound_type(tov, false);
}


static void
text_output_visitor_end_struct(Visitor *v, void **obj)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);
    text_output_visitor_close_compound_type(tov);
}


static void
text_output_visitor_complete(Visitor *v, void *opaque)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);
    char **result = opaque;

    *result = g_string_free(tov->string, false);
    tov->string = NULL;
}


static void
text_output_visitor_free(Visitor *v)
{
    TextOutputVisitor *tov = text_output_visitor_from_visitor(v);

    if (tov->string) {
        g_string_free(tov->string, true);
    }
    g_free(tov);
}


Visitor *
text_output_visitor_new(int extraIndent,
                        int skipLevel)
{
    TextOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->extraIndent = extraIndent;
    v->skipLevel = skipLevel;
    v->string = g_string_new(NULL);
    v->visitor.type = VISITOR_OUTPUT;
    v->visitor.type_int64 = text_output_visitor_print_type_int64;
    v->visitor.type_uint64 = text_output_visitor_print_type_uint64;
    v->visitor.type_size = text_output_visitor_print_type_size;
    v->visitor.type_bool = text_output_visitor_print_type_bool;
    v->visitor.type_str = text_output_visitor_print_type_str;
    v->visitor.type_number = text_output_visitor_print_type_number;
    v->visitor.start_list = text_output_visitor_start_list;
    v->visitor.next_list = text_output_visitor_next_list;
    v->visitor.end_list = text_output_visitor_end_list;
    v->visitor.start_struct = text_output_visitor_start_struct;
    v->visitor.end_struct = text_output_visitor_end_struct;
    v->visitor.complete = text_output_visitor_complete;
    v->visitor.free = text_output_visitor_free;

    QSIMPLEQ_INIT(&v->state);

    return &v->visitor;
}
