/*
 * JSON Parser
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/unicode.h"
#include "qapi/error.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qnull.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "json-parser-int.h"

struct JSONToken {
    JSONTokenType type;
    int x;
    int y;
    char str[];
};

/*
 * Objects: { } | { members }
 * - Empty: { -> AFTER_LCURLY -> }
 * - Non-empty: { -> AFTER_LCURLY -> BEFORE_KEY -> string -> END_OF_KEY -> : ->
 *              BEFORE_VALUE -> value -> END_OF_VALUE -> , -> BEFORE_KEY -> ... -> }
 *
 * Arrays: [ ] | [ elements ]
 * - Empty: [ -> AFTER_LSQUARE -> ]
 * - Non-empty: [ -> AFTER_LSQUARE -> BEFORE_VALUE -> value -> END_OF_VALUE -> , ->
 *              BEFORE_VALUE -> ... -> ]
 *
 * The two cases for END_OF_VALUE are distinguished based on the type of QObject at
 * top-of-stack.
 */
typedef enum JSONParserState {
    AFTER_LCURLY,
    AFTER_LSQUARE,
    BEFORE_KEY,
    BEFORE_VALUE,
    END_OF_KEY,
    END_OF_VALUE,
} JSONParserState;

typedef struct JSONParserStackEntry {
    /* A QString with the last parsed key, or a QList/QDict for the current container.  */
    QObject *partial;

    /* Needed to distinguish whether the parser is waiting for a colon or comma.  */
    JSONParserState state;
} JSONParserStackEntry;

#define BUG_ON(cond) assert(!(cond))

/**
 * TODO
 *
 * 0) make errors meaningful again
 * 1) add geometry information to tokens
 * 3) should we return a parsed size?
 * 4) deal with premature EOI
 */

static inline JSONParserStackEntry *current_entry(JSONParserContext *ctxt)
{
    return g_queue_peek_tail(ctxt->stack);
}

static void push_entry(JSONParserContext *ctxt, QObject *partial, JSONParserState state)
{
    JSONParserStackEntry *entry = g_new(JSONParserStackEntry, 1);
    entry->partial = partial;
    entry->state = state;
    g_queue_push_tail(ctxt->stack, entry);
}

/* Note that entry->partial does *not* lose its reference count even if value == NULL.  */
static JSONParserStackEntry *pop_entry(JSONParserContext *ctxt, QObject **value)
{
    JSONParserStackEntry *entry = g_queue_pop_tail(ctxt->stack);
    if (value) {
        *value = entry->partial;
    }
    g_free(entry);
    return current_entry(ctxt);
}

/**
 * Error handler
 */
static void G_GNUC_PRINTF(3, 4) parse_error(JSONParserContext *ctxt,
                                           const JSONToken *token, const char *msg, ...)
{
    va_list ap;
    char message[1024];

    if (ctxt->err) {
        return;
    }
    va_start(ap, msg);
    vsnprintf(message, sizeof(message), msg, ap);
    va_end(ap);
    error_setg(&ctxt->err, "JSON parse error, %s", message);
}

static int cvt4hex(const char *s)
{
    int cp, i;

    cp = 0;
    for (i = 0; i < 4; i++) {
        if (!qemu_isxdigit(s[i])) {
            return -1;
        }
        cp <<= 4;
        if (s[i] >= '0' && s[i] <= '9') {
            cp |= s[i] - '0';
        } else if (s[i] >= 'a' && s[i] <= 'f') {
            cp |= 10 + s[i] - 'a';
        } else if (s[i] >= 'A' && s[i] <= 'F') {
            cp |= 10 + s[i] - 'A';
        } else {
            return -1;
        }
    }
    return cp;
}

/**
 * parse_string(): Parse a JSON string
 *
 * From RFC 8259 "The JavaScript Object Notation (JSON) Data
 * Interchange Format":
 *
 *    char = unescaped /
 *        escape (
 *            %x22 /          ; "    quotation mark  U+0022
 *            %x5C /          ; \    reverse solidus U+005C
 *            %x2F /          ; /    solidus         U+002F
 *            %x62 /          ; b    backspace       U+0008
 *            %x66 /          ; f    form feed       U+000C
 *            %x6E /          ; n    line feed       U+000A
 *            %x72 /          ; r    carriage return U+000D
 *            %x74 /          ; t    tab             U+0009
 *            %x75 4HEXDIG )  ; uXXXX                U+XXXX
 *    escape = %x5C              ; \
 *    quotation-mark = %x22      ; "
 *    unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
 *
 * Extensions over RFC 8259:
 * - Extra escape sequence in strings:
 *   0x27 (apostrophe) is recognized after escape, too
 * - Single-quoted strings:
 *   Like double-quoted strings, except they're delimited by %x27
 *   (apostrophe) instead of %x22 (quotation mark), and can't contain
 *   unescaped apostrophe, but can contain unescaped quotation mark.
 *
 * Note:
 * - Encoding is modified UTF-8.
 * - Invalid Unicode characters are rejected.
 * - Control characters \x00..\x1F are rejected by the lexer.
 */
static QString *parse_string(JSONParserContext *ctxt, const JSONToken *token)
{
    const char *ptr = token->str;
    GString *str;
    char quote;
    const char *beg;
    int cp, trailing;
    char *end;
    ssize_t len;
    char utf8_buf[5];

    assert(*ptr == '"' || *ptr == '\'');
    quote = *ptr++;
    str = g_string_new(NULL);

    while (*ptr != quote) {
        assert(*ptr);
        switch (*ptr) {
        case '\\':
            beg = ptr++;
            switch (*ptr++) {
            case '"':
                g_string_append_c(str, '"');
                break;
            case '\'':
                g_string_append_c(str, '\'');
                break;
            case '\\':
                g_string_append_c(str, '\\');
                break;
            case '/':
                g_string_append_c(str, '/');
                break;
            case 'b':
                g_string_append_c(str, '\b');
                break;
            case 'f':
                g_string_append_c(str, '\f');
                break;
            case 'n':
                g_string_append_c(str, '\n');
                break;
            case 'r':
                g_string_append_c(str, '\r');
                break;
            case 't':
                g_string_append_c(str, '\t');
                break;
            case 'u':
                cp = cvt4hex(ptr);
                ptr += 4;

                /* handle surrogate pairs */
                if (cp >= 0xD800 && cp <= 0xDBFF
                    && ptr[0] == '\\' && ptr[1] == 'u') {
                    /* leading surrogate followed by \u */
                    cp = 0x10000 + ((cp & 0x3FF) << 10);
                    trailing = cvt4hex(ptr + 2);
                    if (trailing >= 0xDC00 && trailing <= 0xDFFF) {
                        /* followed by trailing surrogate */
                        cp |= trailing & 0x3FF;
                        ptr += 6;
                    } else {
                        cp = -1; /* invalid */
                    }
                }

                if (mod_utf8_encode(utf8_buf, sizeof(utf8_buf), cp) < 0) {
                    parse_error(ctxt, token,
                                "%.*s is not a valid Unicode character",
                                (int)(ptr - beg), beg);
                    goto out;
                }
                g_string_append(str, utf8_buf);
                break;
            default:
                parse_error(ctxt, token, "invalid escape sequence in string");
                goto out;
            }
            break;
        case '%':
            if (ctxt->ap) {
                if (ptr[1] != '%') {
                    parse_error(ctxt, token, "can't interpolate into string");
                    goto out;
                }
                ptr++;
            }
            /* fall through */
        default:
            cp = mod_utf8_codepoint(ptr, 6, &end);
            if (cp < 0) {
                parse_error(ctxt, token, "invalid UTF-8 sequence in string");
                goto out;
            }
            ptr = end;
            len = mod_utf8_encode(utf8_buf, sizeof(utf8_buf), cp);
            assert(len >= 0);
            g_string_append(str, utf8_buf);
        }
    }

    return qstring_from_gstring(str);

out:
    g_string_free(str, true);
    return NULL;
}

/* Terminals  */

static QObject *parse_keyword(JSONParserContext *ctxt, const JSONToken *token)
{
    assert(token && token->type == JSON_KEYWORD);

    if (!strcmp(token->str, "true")) {
        return QOBJECT(qbool_from_bool(true));
    } else if (!strcmp(token->str, "false")) {
        return QOBJECT(qbool_from_bool(false));
    } else if (!strcmp(token->str, "null")) {
        return QOBJECT(qnull());
    }
    parse_error(ctxt, token, "invalid keyword '%s'", token->str);
    return NULL;
}

static QObject *parse_interpolation(JSONParserContext *ctxt, const JSONToken *token)
{
    assert(token && token->type == JSON_INTERP);

    if (!strcmp(token->str, "%p")) {
        return va_arg(*ctxt->ap, QObject *);
    } else if (!strcmp(token->str, "%i")) {
        return QOBJECT(qbool_from_bool(va_arg(*ctxt->ap, int)));
    } else if (!strcmp(token->str, "%d")) {
        return QOBJECT(qnum_from_int(va_arg(*ctxt->ap, int)));
    } else if (!strcmp(token->str, "%ld")) {
        return QOBJECT(qnum_from_int(va_arg(*ctxt->ap, long)));
    } else if (!strcmp(token->str, "%lld")) {
        return QOBJECT(qnum_from_int(va_arg(*ctxt->ap, long long)));
    } else if (!strcmp(token->str, "%" PRId64)) {
        return QOBJECT(qnum_from_int(va_arg(*ctxt->ap, int64_t)));
    } else if (!strcmp(token->str, "%u")) {
        return QOBJECT(qnum_from_uint(va_arg(*ctxt->ap, unsigned int)));
    } else if (!strcmp(token->str, "%lu")) {
        return QOBJECT(qnum_from_uint(va_arg(*ctxt->ap, unsigned long)));
    } else if (!strcmp(token->str, "%llu")) {
        return QOBJECT(qnum_from_uint(va_arg(*ctxt->ap, unsigned long long)));
    } else if (!strcmp(token->str, "%" PRIu64)) {
        return QOBJECT(qnum_from_uint(va_arg(*ctxt->ap, uint64_t)));
    } else if (!strcmp(token->str, "%s")) {
        return QOBJECT(qstring_from_str(va_arg(*ctxt->ap, const char *)));
    } else if (!strcmp(token->str, "%f")) {
        return QOBJECT(qnum_from_double(va_arg(*ctxt->ap, double)));
    }
    parse_error(ctxt, token, "invalid interpolation '%s'", token->str);
    return NULL;
}

static QObject *parse_literal(JSONParserContext *ctxt, const JSONToken *token)
{
    assert(token);

    switch (token->type) {
    case JSON_STRING:
        return QOBJECT(parse_string(ctxt, token));
    case JSON_INTEGER: {
        /*
         * Represent JSON_INTEGER as QNUM_I64 if possible, else as
         * QNUM_U64, else as QNUM_DOUBLE.  Note that qemu_strtoi64()
         * and qemu_strtou64() fail with ERANGE when it's not
         * possible.
         *
         * qnum_get_int() will then work for any signed 64-bit
         * JSON_INTEGER, qnum_get_uint() for any unsigned 64-bit
         * integer, and qnum_get_double() both for any JSON_INTEGER
         * and any JSON_FLOAT (with precision loss for integers beyond
         * 53 bits)
         */
        int ret;
        int64_t value;
        uint64_t uvalue;

        ret = qemu_strtoi64(token->str, NULL, 10, &value);
        if (!ret) {
            return QOBJECT(qnum_from_int(value));
        }
        assert(ret == -ERANGE);

        if (token->str[0] != '-') {
            ret = qemu_strtou64(token->str, NULL, 10, &uvalue);
            if (!ret) {
                return QOBJECT(qnum_from_uint(uvalue));
            }
            assert(ret == -ERANGE);
        }
    }
    /* fall through to JSON_FLOAT */
    case JSON_FLOAT:
        /* FIXME dependent on locale; a pervasive issue in QEMU */
        /* FIXME our lexer matches RFC 8259 in forbidding Inf or NaN,
         * but those might be useful extensions beyond JSON */
        return QOBJECT(qnum_from_double(strtod(token->str, NULL)));
    default:
        abort();
    }
}

/* Parsing state machine  */

static QObject *parse_begin_value(JSONParserContext *ctxt, const JSONToken *token)
{
    switch (token->type) {
    case JSON_LCURLY:
        push_entry(ctxt, QOBJECT(qdict_new()), AFTER_LCURLY);
        return NULL;
    case JSON_LSQUARE:
        push_entry(ctxt, QOBJECT(qlist_new()), AFTER_LSQUARE);
        return NULL;
    case JSON_INTERP:
        return parse_interpolation(ctxt, token);
    case JSON_INTEGER:
    case JSON_FLOAT:
    case JSON_STRING:
        return parse_literal(ctxt, token);
    case JSON_KEYWORD:
        return parse_keyword(ctxt, token);
    default:
        parse_error(ctxt, token, "expecting value");
        return NULL;
    }
}

static QObject *json_parser_parse_token(JSONParserContext *ctxt, const JSONToken *token)
{
    JSONParserStackEntry *entry;
    JSONParserState state;
    QString *key;
    QObject *value = NULL;

    entry = current_entry(ctxt);
    state = entry ? entry->state : BEFORE_VALUE;
    switch (state) {
    case AFTER_LCURLY:
        /* Grab '}' for empty object or fall through to BEFORE_KEY */
        if (token->type == JSON_RCURLY) {
            entry = pop_entry(ctxt, &value);
            break;
        }
        entry->state = BEFORE_KEY;
        /* fall through */

    case BEFORE_KEY:
        /* Expecting object key */
        if (token->type == JSON_STRING) {
            key = parse_string(ctxt, token);
            if (!key) {
                return NULL;
            }

            /* Store key in a special entry on the stack */
            push_entry(ctxt, QOBJECT(key), END_OF_KEY);
        } else {
            parse_error(ctxt, token, "expecting key");
        }
        return NULL;

    case END_OF_KEY:
        /* Expecting ':' after key */
        if (token->type == JSON_COLON) {
            entry->state = BEFORE_VALUE;
        } else {
            parse_error(ctxt, token, "expecting ':'");
        }
        return NULL;

    case AFTER_LSQUARE:
        /* Grab ']' for empty array or fall through to BEFORE_VALUE */
        if (token->type == JSON_RSQUARE) {
            entry = pop_entry(ctxt, &value);
            break;
        }
        entry->state = BEFORE_VALUE;
        /* fall through */

    case BEFORE_VALUE:
        /* Expecting value */
        value = parse_begin_value(ctxt, token);
        if (!value) {
            /* Error or '['/'{' */
            return NULL;
        }
        /* Return value or insert it into a container */
        break;

    case END_OF_VALUE:
        /* Grab ',' or ']' for array; ',' or '}' for object */
        if (qobject_to(QList, entry->partial)) {
            /* Array */
            if (token->type != JSON_RSQUARE) {
                if (token->type == JSON_COMMA) {
                    entry->state = BEFORE_VALUE;
                } else {
                    parse_error(ctxt, token, "expected ',' or ']'");
                }
                return NULL;
            }
        } else if (qobject_to(QDict, entry->partial)) {
            /* Object */
            if (token->type != JSON_RCURLY) {
                if (token->type == JSON_COMMA) {
                    entry->state = BEFORE_KEY;
                } else {
                    parse_error(ctxt, token, "expected ',' or '}'");
                }
                return NULL;
            }
        } else {
            g_assert_not_reached();
        }

        /* Got ']' or '}', return value or insert into parent container */
        entry = pop_entry(ctxt, &value);
        break;
    }

    assert(value);
    if (entry == NULL) {
        /* The toplevel value is complete.  */
        return value;
    }

    key = qobject_to(QString, entry->partial);
    if (key) {
        const char *key_str;
        QDict *dict;

        entry = pop_entry(ctxt, NULL);
        dict = qobject_to(QDict, entry->partial);
        assert(dict);
        key_str = qstring_get_str(key);
        if (qdict_haskey(dict, key_str)) {
            parse_error(ctxt, token, "duplicate key");
            qobject_unref(value);
            return NULL;
        }
        qdict_put_obj(dict, key_str, value);
        qobject_unref(key);
    } else {
        /* Add to array */
        qlist_append_obj(qobject_to(QList, entry->partial), value);
    }

    entry->state = END_OF_VALUE;
    return NULL;
}

JSONToken *json_token(JSONTokenType type, int x, int y, GString *tokstr)
{
    JSONToken *token = g_malloc(sizeof(JSONToken) + tokstr->len + 1);

    token->type = type;
    memcpy(token->str, tokstr->str, tokstr->len);
    token->str[tokstr->len] = 0;
    token->x = x;
    token->y = y;
    return token;
}

void json_parser_reset(JSONParserContext *ctxt)
{
    JSONParserStackEntry *entry;

    ctxt->err = NULL;
    while ((entry = g_queue_pop_tail(ctxt->stack)) != NULL) {
        qobject_unref(entry->partial);
        g_free(entry);
    }
}

void json_parser_init(JSONParserContext *ctxt, va_list *ap)
{
    ctxt->stack = g_queue_new();
    ctxt->ap = ap;
    json_parser_reset(ctxt);
}

void json_parser_destroy(JSONParserContext *ctxt)
{
    json_parser_reset(ctxt);
    g_queue_free(ctxt->stack);
    ctxt->stack = NULL;
}

QObject *json_parser_feed(JSONParserContext *ctxt, const JSONToken *token, Error **errp)
{
    QObject *result = NULL;

    assert(!ctxt->err);
    switch (token->type) {
    case JSON_ERROR:
        parse_error(ctxt, token, "JSON parse error, stray '%s'", token->str);
        break;

    case JSON_END_OF_INPUT:
        /* Check for premature end of input */
        if (!g_queue_is_empty(ctxt->stack)) {
            parse_error(ctxt, token, "premature end of input");
        }
        break;

    default:
        result = json_parser_parse_token(ctxt, token);
        break;
    }

    error_propagate(errp, ctxt->err);
    return result;
}
