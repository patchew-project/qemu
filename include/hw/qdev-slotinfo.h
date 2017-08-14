#ifndef QDEV_SLOTINFO_H
#define QDEV_SLOTINFO_H

#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qstring.h"

/**
 * valuelist_contains:
 *
 * Returns true if the value list represented by @values
 * contains @v.
 *
 * @values follows the format documented at SlotOption.values
 * in the QAPI schema.
 */
bool valuelist_contains(QObject *values, QObject *v);

/**
 * valuelist_extend:
 *
 * Extend a value list with elements from another value list.
 *
 * Ownership of 'new' is transfered to the function.
 */
void valuelist_extend(QObject **valuelist, QObject *new);

/*
 * TODO: Use more efficient data structurs (instead of
 * DeviceSlotInfoList and SlotOptionList) when building the list
 * and combining items.
 */

/**
 * slot_options_can_be_combined:
 *
 * Check if two SlotOptionLists can be combined in one.
 *
 * Two slot option lists can be combined if all options have exactly
 * the same value except (at most) one.
 *
 * Returns true if the option lists can be combined.
 *
 * If return value is true, *@opt_name is set to the only
 * mismatching option name.  If all options match, *@opt_name is
 * set to NULL.
 */
bool slot_options_can_be_combined(SlotOptionList *a, SlotOptionList *b,
                                  const char **opt_name);

/*TODO: doc */
bool slots_can_be_combined(DeviceSlotInfo *a, DeviceSlotInfo *b,
	                       const char **opt_name);

/*TODO: doc */
void slots_combine(DeviceSlotInfo *a, DeviceSlotInfo *b, const char *opt_name);

/*TODO: doc */
bool slots_try_combine(DeviceSlotInfo *a, DeviceSlotInfo *b);

/*TODO: doc */
void slot_list_add_slot(DeviceSlotInfoList **l, DeviceSlotInfo *slot);

/*TODO: doc */
DeviceSlotInfoList *slot_list_collapse(DeviceSlotInfoList *l);

/*TODO: doc */
void slot_add_opt(DeviceSlotInfo *slot, const char *option, QObject *values);

#define slot_add_opt_str(slot, option, s) \
    slot_add_opt(slot, option, QOBJECT(qstring_from_str(s)));

#define slot_add_opt_int(slot, option, i) \
    slot_add_opt(slot, option, QOBJECT(qnum_from_int(i)));

SlotOption *slot_options_find_opt(SlotOptionList *opts, const char *option);

static inline SlotOption *slot_find_opt(DeviceSlotInfo *slot, const char *option)
{
	return slot_options_find_opt(slot->opts, option);
}

/*TODO: doc */
DeviceSlotInfo *make_slot(BusState *bus);

#endif /* QDEV_SLOTINFO_H */
