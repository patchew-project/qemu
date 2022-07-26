/*
 * QAPI util functions
 *
 * Copyright Fujitsu, Inc. 2014
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_UTIL_H
#define QAPI_UTIL_H

typedef enum {
    QAPI_DEPRECATED,
    QAPI_UNSTABLE,
} QapiSpecialFeature;

typedef struct QEnumLookup {
    const char *const *array;
    const unsigned char *const special_features;
    const int size;
} QEnumLookup;

struct strList;

const char *qapi_enum_lookup(const QEnumLookup *lookup, int val);
int qapi_enum_parse(const QEnumLookup *lookup, const char *buf,
                    int def, Error **errp);
bool qapi_bool_parse(const char *name, const char *value, bool *obj,
                     Error **errp);

int parse_qapi_name(const char *name, bool complete);

/*
 * Produce and return a NULL-terminated array of strings from @args.
 * All strings are g_strdup'd.
 */
GStrv strv_from_strList(const struct strList *args);

/*
 * Produce a strList from the character delimited string @in.
 * All strings are g_strdup'd.
 * A NULL or empty input string returns NULL.
 */
struct strList *strList_from_string(const char *in, char delim);

/*
 * For any GenericList @list, insert @element at the front.
 *
 * Note that this macro evaluates @element exactly once, so it is safe
 * to have side-effects with that argument.
 */
#define QAPI_LIST_PREPEND(list, element) do { \
    typeof(list) _tmp = g_malloc(sizeof(*(list))); \
    _tmp->value = (element); \
    _tmp->next = (list); \
    (list) = _tmp; \
} while (0)

/*
 * For any pointer to a GenericList @tail (usually the 'next' member of a
 * list element), insert @element at the back and update the tail.
 *
 * Note that this macro evaluates @element exactly once, so it is safe
 * to have side-effects with that argument.
 */
#define QAPI_LIST_APPEND(tail, element) do { \
    *(tail) = g_malloc0(sizeof(**(tail))); \
    (*(tail))->value = (element); \
    (tail) = &(*(tail))->next; \
} while (0)

/*
 * For any GenericList @list, return its length.
 */
#define QAPI_LIST_LENGTH(list) \
    ({ \
        int len = 0; \
        typeof(list) elem; \
        for (elem = list; elem != NULL; elem = elem->next) { \
            len++; \
        } \
        len; \
    })

#endif
