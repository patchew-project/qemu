
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/mpam.h"
#include "qom/object.h"
#include "qapi/qapi-commands-mpam.h"

typedef struct MPAMQueryState {
    Error **errp;
    MpamCacheInfoList **head;
    bool level_filter_on;
    int level;
} MPAMQueryState;

static int mpam_query_cache(Object *obj, void *opaque)
{
    MPAMQueryState *state = opaque;
    MpamCacheInfoList *infolist;
    MpamCacheInfo *info;

    if (!object_dynamic_cast(obj, TYPE_MPAM_MSC_CACHE)) {
        return 0;
    }
    if (state->level_filter_on &&
        object_property_get_uint(obj, "cache-level", state->errp) !=
        state->level) {
        return 0;
    }

    infolist = g_malloc0(sizeof(*infolist));
    info = g_malloc0(sizeof(*info));

    mpam_cache_fill_info(obj, info);

    infolist->value = info;

    *state->head = infolist;
    state->head = &infolist->next;

    return 0;
}

MpamCacheInfoList *qmp_query_mpam_cache(bool has_level, int64_t level,
                                        Error **errp)
{

    MpamCacheInfoList *head = NULL;
    MPAMQueryState state = {
        .errp = errp,
        .head = &head,
        .level_filter_on = has_level,
        .level = level,
    };

    object_child_foreach_recursive(object_get_root(), mpam_query_cache, &state);

    return head;
}
