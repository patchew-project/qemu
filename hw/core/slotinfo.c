#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/qdev-slotinfo.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qjson.h"
#include "qapi/util.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi-visit.h"

//#define DEBUG_QOBJECTS

#ifdef DEBUG_QOBJECTS
#define _DBG(args...) fprintf(stderr, args);

#define DBG(args...) do { _DBG("%s: ", __FUNCTION__); \
                          _DBG(args); \
                     } while (0)
#define QDBG(fmt, obj, args...) do { \
        QString *js = qobject_to_json(obj); \
        DBG(fmt ## args); \
        _DBG(": %s\n", qstring_get_str(js)); \
        QDECREF(js); \
    } while (0)
#else
#define DBG(...) do { } while (0)
#define QDBG(...) do { } while (0)
#endif

/* Ensure a value list is normalized to a list of values
 *
 * This does NOT normalize individual elements of the list to be
 * in the [A] or [A, B] format.
 *
 * Returns a new reference to the normalized value.
 */
static QList *valuelist_normalize(QObject *values)
{
    if (qobject_type(values) == QTYPE_QLIST) {
        qobject_incref(values);
        return qobject_to_qlist(values);
    } else {
        QList *l = qlist_new();

        qobject_incref(values);
        qlist_append_obj(l, values);
        return l;
    }
}

/* Simplify value list, if possible
 *
 * Onwership of @values is transfered to the function, and a
 * new object is returned.
 */
static QObject *valuelist_simplify(QList *values)
{
    if (qlist_size(values) == 1) {
        QObject *o = qlist_entry_obj(qlist_first(values));
        QType t = qobject_type(o);
        if (t == QTYPE_QNULL ||
            t == QTYPE_QNUM ||
            t == QTYPE_QSTRING ||
            t == QTYPE_QBOOL) {
            qobject_incref(o);
            QDECREF(values);
            return o;
        }
    }

    return QOBJECT(values);
}

/* Check if a given single-element value can be represented as a [A, B] range */
static bool value_can_be_range(QObject *v)
{
    QType t = qobject_type(v);
    return (t == QTYPE_QNUM || t == QTYPE_QSTRING);
}

/*
 * Represent a single value (if max is NULL), or a [min, max] range
 * if @max is not NULL
 */
typedef struct ValueRange {
    QObject *min, *max;
} ValueRange;

/*
 * Validate a value list element and put result on ValueRange
 *
 * NOTE: this won't increase ref count of @vr->min and @vr->max
 */
static bool valuelist_element_get_range(QObject *elm, ValueRange *vr)
{
    QObject *min = NULL, *max = NULL;

    if (qobject_type(elm) == QTYPE_QLIST) {
        QList *l = qobject_to_qlist(elm);
        if (qlist_size(l) < 1 || qlist_size(l) > 2) {
            return false;
        }
        min = qlist_entry_obj(qlist_first(l));
        if (qlist_size(l) == 2) {
            max = qlist_entry_obj(qlist_next(qlist_first(l)));
        }
    } else {
        min = elm;
    }

    assert(min);
    /* Invalid range: */
    if (max && (!value_can_be_range(min) || !value_can_be_range(max) ||
                qobject_type(min) != qobject_type(max))) {
        return false;
    }

    /* If the value can be a range, make it a range */
    if (!max && value_can_be_range(min)) {
        max = min;
    }

    vr->min = min;
    vr->max = max;
    return true;
}

/* Check if @v is inside the @vr range */
static bool range_contains(ValueRange *vr, QObject *v)
{
    assert(vr->min);
    if (vr->max) { /* range */
        return qobject_type(vr->min) == qobject_type(v) &&
               qobject_compare(vr->max, v) >= 0 &&
               qobject_compare(v, vr->min) >= 0;
    } else {  /* single element */
        return qobject_compare(vr->min, v) == 0;
    }
}

/* Check if @a contains @b */
static bool range_contains_range(ValueRange *a, ValueRange *b)
{
    bool r = range_contains(a, b->min);
    if (b->max) {
        r &= range_contains(a, b->max);
    }
    return r;
}

/* Check if the intersection of @a and @b is not empty */
static bool range_overlaps_range(ValueRange *a, ValueRange *b)
{
    return range_contains(a, b->min) ||
           (b->max && range_contains(a, b->max)) ||
           range_contains_range(b, a);
}

/*
 * Check if a given entry of a value list contains the range @minv-@maxv
 * If @maxv is NULL, only check if the entry contains @minv
 */
static bool valuelist_entry_contains(QObject *ev, ValueRange *vr)
{
    ValueRange er;

    if (!valuelist_element_get_range(ev, &er)) {
        return false;
    }
    return range_contains_range(&er, vr);
}

/*
 * Check if a given entry of a value list contains the range @minv-@maxv
 * If @maxv is NULL, only check if the entry contains @minv
 */
static bool valuelist_entry_overlaps(QObject *ev, ValueRange *vr)
{
    ValueRange er;

    if (!valuelist_element_get_range(ev, &er)) {
        return false;
    }

    return range_overlaps_range(&er, vr);
}

/*
 * Find the entry in the value list that contains the range @minv-@maxv
 * If @maxv is NULL, check if the value list contains @minv
 *
 * Returns the list entry that contains the range, or NULL if
 * not found.
 */
static QListEntry *nvaluelist_find_range_match(QList *l, ValueRange *vr)
{
    QListEntry *e;

    QLIST_FOREACH_ENTRY(l, e) {
        QObject *ev = qlist_entry_obj(e);
        if (valuelist_entry_contains(ev, vr)) {
            return e;
        }
    }

    return NULL;
}

/*
 * Find the entry in the value list that contains @v
 *
 * Returns the list entry that contains the range, or NULL if
 * not found.
 */
static QListEntry *nvaluelist_find_value_match(QList *l, QObject *v)
{
    ValueRange vr = { .min = v };
    return nvaluelist_find_range_match(l, &vr);
}

/*
 * Try to sum i to number, if it's an integer.
 * Otherwise, just return a new reference to @v
 */
static QObject *qnum_try_int_add(QObject *v, int i)
{
    QNum *qn;
    uint64_t u64;
    int64_t i64;

    if (qobject_type(v) != QTYPE_QNUM) {
        qobject_incref(v);
        return v;
    }

    /*TODO: we should be able to convert uint to int and vice-versa. e.g.:
     * - qnum_try_int_add(qnum_from_int(INT64_MAX), 1)
     * - qnum_try_int_add(qnum_from_uint(UINT64_MIN), -1)
     */
    qn = qobject_to_qnum(v);
    if (qnum_get_try_int(qn, &i64)) {
        if ((i < 0) && INT64_MIN - i >= i64) {
            i64 = INT64_MIN;
        } else if ((i > 0) && INT64_MAX - i <= i64) {
            i64 = INT64_MAX;
        } else {
            i64 = i64 + i;
        }
        return QOBJECT(qnum_from_int(i64));
    } else if (qnum_get_try_uint(qn, &u64)) {
        if ((i < 0) && -i >= u64) {
            u64 = 0;
        } else if ((i > 0) && UINT64_MAX - i <= u64) {
            u64 = UINT64_MAX;
        } else {
            u64 = u64 + i;
        }
        return QOBJECT(qnum_from_uint(u64));
    } else {
        qobject_incref(v);
        return v;
    }
}

/*
 * Look for any entry that overlaps or touches @vr
 * If @skip is not NULL, @skip is not considered as a match.
 */
static QListEntry *nvaluelist_find_overlap(QList *l, ValueRange *vr,
                                           QListEntry *skip)
{
    QListEntry *r = NULL;
    QListEntry *e;
    ValueRange key;

    key.min = qnum_try_int_add(vr->min, -1);
    key.max = qnum_try_int_add(vr->max, 1);

    QLIST_FOREACH_ENTRY(l, e) {
        QObject *ev = qlist_entry_obj(e);
        if (e == skip) {
            continue;
        }
        if (valuelist_entry_overlaps(ev, &key)) {
            r = e;
            break;
        }
    }

    qobject_decref(key.min);
    qobject_decref(key.max);
    return r;
}

bool valuelist_contains(QObject *values, QObject *v)
{
    QList *l = valuelist_normalize(values);
    bool r = !!nvaluelist_find_value_match(l, v);

    QDECREF(l);
    return r;
}

static QListEntry *valuelist_try_overlap(QList *l, ValueRange *vr,
                                         QListEntry *skip)
{
    ValueRange ovr;
    QList *newrange;
    QListEntry *ov = nvaluelist_find_overlap(l, vr, skip);

    if (!ov) {
        return NULL;
    }

    valuelist_element_get_range(ov->value, &ovr);
    if (qobject_compare(ovr.min, vr->min) > 0) {
        ovr.min = vr->min;
    }
    if (qobject_compare(vr->max, ovr.max) > 0) {
        ovr.max = vr->max;
    }

    newrange = qlist_new();
    qobject_incref(ovr.min);
    qlist_append_obj(newrange, ovr.min);
    qobject_incref(ovr.max);
    qlist_append_obj(newrange, ovr.max);

    /*FIXME: this is a hack */
    qobject_decref(ov->value);
    ov->value = QOBJECT(newrange);
    return ov;
}

/* Ownership of @e is passed to the function */
static QListEntry *valuelist_try_merge(QList *l, QListEntry *e)
{
    ValueRange vr;
    QListEntry *ov;

    if (!valuelist_element_get_range(e->value, &vr)) {
        return NULL;
    }

    ov = valuelist_try_overlap(l, &vr, e);
    assert(ov != e);
    if (ov) {
        /*TODO: this is a hack */
        QTAILQ_REMOVE(&l->head, e, next);
        qobject_decref(e->value);
        g_free(e);
    }
    return ov;
}

/* Ownership of @elm is NOT given to the function: only the reference
 * count is increased if necessary.
 */
static void valuelist_append_element(QList *l, QObject *elm)
{
    ValueRange vr;

    if (valuelist_element_get_range(elm, &vr)) {
        QListEntry *ov;

        if (nvaluelist_find_range_match(l, &vr)) {
            return;
        }

        ov = valuelist_try_overlap(l, &vr, NULL);
        /* If we find an overlapping entry, keep trying to merge it with
         * other elements.
         */
        if (ov) {
            while (ov) {
                ov = valuelist_try_merge(l, ov);
            }
            return;
        }
    }

    /* No overlap found, just append element to the list */
    qobject_incref(elm);
    qlist_append_obj(l, elm);
}

void valuelist_extend(QObject **valuelist, QObject *new)
{
    QObject *old = *valuelist;
    QList *l = valuelist_normalize(old);
    QList *newl = valuelist_normalize(new);
    QListEntry *e;

    QLIST_FOREACH_ENTRY(newl, e) {
        QObject *elm = qlist_entry_obj(e);
        valuelist_append_element(l, elm);
    }
    QDECREF(newl);

    *valuelist = valuelist_simplify(l);
    qobject_decref(old);
}

SlotOption *slot_options_find_opt(SlotOptionList *opts, const char *option)
{
    for (; opts; opts = opts->next) {
        if (!strcmp(opts->value->option, option)) {
            return opts->value;
        }
    }
    return NULL;
}

bool slot_options_can_be_combined(SlotOptionList *a, SlotOptionList *b,
                                  const char **opt_name)
{
    SlotOptionList *ol;
    const char *mismatch = NULL;

    /* Check if all options in @b will be handled when we loop through @a */
    for (ol = b; ol; ol = ol->next) {
        if (!slot_options_find_opt(a, ol->value->option)) {
            return false;
        }
    }

    for (ol = a; ol; ol = ol->next) {
        SlotOption *ao = ol->value;
        SlotOption *bo = slot_options_find_opt(b, ao->option);

        if (!bo) {
            return false;
        }

        if (qobject_compare(bo->values, ao->values)) {
            if (mismatch && strcmp(mismatch, ao->option)) {
                return false;
            }

            mismatch = ao->option;
        }
    }

    if (opt_name) {
        *opt_name = mismatch;
    }
    return true;
}

static int compare_strList(strList *a, strList *b)
{
    for (; a && b; a = a->next, b = b->next) {
        int c = strcmp(a->value, b->value);
        if (c) {
            return c;
        }
    }

    if (b) {
        return -1;
    } else if (a) {
        return 1;
    } else {
        return 0;
    }

}

bool slots_can_be_combined(DeviceSlotInfo *a, DeviceSlotInfo *b,
                           const char **opt_name)
{
    if (a->available != b->available ||
        a->hotpluggable != b->hotpluggable ||
        a->has_count != b->has_count ||
        a->opts_complete != b->opts_complete ||
        a->has_device || b->has_device ||
        compare_strList(a->device_types, b->device_types)) {
        return false;
    }

    return slot_options_can_be_combined(a->opts, b->opts, opt_name);
}

void slots_combine(DeviceSlotInfo *a, DeviceSlotInfo *b, const char *opt_name)
{
    assert(slots_can_be_combined(a, b, NULL));
    if (a->has_count) {
        a->count += b->count;
    }
    if (opt_name) {
        SlotOption *aopt = slot_options_find_opt(a->opts, opt_name);
        SlotOption *bopt = slot_options_find_opt(b->opts, opt_name);

        valuelist_extend(&aopt->values, bopt->values);
    }
}

bool slots_try_combine(DeviceSlotInfo *a, DeviceSlotInfo *b)
{
    const char *opt = NULL;
    assert(a != b);

    if (slots_can_be_combined(a, b, &opt)) {
        slots_combine(a, b, opt);
        return true;
    }

    return false;
}

/* Try to combine @slot with an entry in @l
 *
 * Will return a pointer to the 'next' pointer in the previous entry,
 * to allow callers to remove the entry from the list if necessary.
 */
static DeviceSlotInfoList **slot_list_try_combine_slot(DeviceSlotInfoList **l, DeviceSlotInfo *slot)
{
    DeviceSlotInfoList **pprev;

    for (pprev = l; *pprev; pprev = &(*pprev)->next) {
        DeviceSlotInfo *i = (*pprev)->value;
        if (slots_try_combine(i, slot)) {
            return pprev;
        }
    }

    return NULL;
}

DeviceSlotInfoList *slot_list_collapse(DeviceSlotInfoList *l)
{
    DeviceSlotInfoList *newlist = NULL;
    DeviceSlotInfoList *queue = l;

    while (queue) {
        DeviceSlotInfoList **pprev;
        DeviceSlotInfoList *next = queue->next;

        pprev = slot_list_try_combine_slot(&newlist, queue->value);
        if (pprev) {
            DeviceSlotInfoList *removed = *pprev;
            /* Remove modified item from @newlist@ */
            *pprev = (*pprev)->next;
            /* Dequeue item from @queue */
            queue->next = NULL;
            qapi_free_DeviceSlotInfoList(queue);
            /* Queue modified item on @queue */
            removed->next = next;
            queue = removed;
        } else {
            /* Not combined, just insert into newlist */
            queue->next = newlist;
            newlist = queue;
            queue = next;
        }
    }

    return newlist;
}

/* Ownership of @slot is given to the function */
void slot_list_add_slot(DeviceSlotInfoList **l, DeviceSlotInfo *slot)
{
    DeviceSlotInfoList *li;

    /* Avoid adding new entry if it can be combined */
    if (slot_list_try_combine_slot(l, slot)) {
        qapi_free_DeviceSlotInfo(slot);
        return;
    }

    li = g_new0(DeviceSlotInfoList, 1);
    li->value = slot;
    li->next = *l;
    *l = li;
}

void slot_add_opt(DeviceSlotInfo *slot, const char *option, QObject *values)
{
    SlotOptionList *l =  g_new0(SlotOptionList, 1);

    l->value = g_new0(SlotOption, 1);
    l->value->option = g_strdup(option);
    l->value->values = values;
    l->next = slot->opts;
    slot->opts = l;
}

/*TODO: move it to common code */
static inline bool qbus_is_full(BusState *bus)
{
    BusClass *bus_class = BUS_GET_CLASS(bus);
    return bus_class->max_dev && bus->max_index >= bus_class->max_dev;
}

DeviceSlotInfo *make_slot(BusState *bus)
{
    DeviceSlotInfo *s = g_new0(DeviceSlotInfo, 1);

    s->device_types = g_new0(strList, 1);
    s->device_types->value = g_strdup(BUS_GET_CLASS(bus)->device_type);
    s->hotpluggable = qbus_is_hotpluggable(bus);
    s->available = !qbus_is_full(bus);

    slot_add_opt_str(s, "bus", bus->name);

    return s;
}
