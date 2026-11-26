/*
 * Hexagon PMU QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_pmu.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

uint16_t hexagon_pmu_get_event(HexagonPMUState *s, unsigned int index)
{
    g_assert(index < NUM_PMU_CTRS);
    return s->events[index];
}

void hexagon_pmu_set_event(HexagonPMUState *s, unsigned int index,
                           uint16_t event)
{
    g_assert(index < NUM_PMU_CTRS);
    s->events[index] = event;
}

uint32_t hexagon_pmu_get_offset(HexagonPMUState *s, unsigned int index)
{
    g_assert(index < NUM_PMU_CTRS);
    return s->ctr_offset[index];
}

void hexagon_pmu_set_offset(HexagonPMUState *s, unsigned int index,
                            uint32_t offset)
{
    g_assert(index < NUM_PMU_CTRS);
    s->ctr_offset[index] = offset;
}

uint32_t hexagon_pmu_get_thread_mask(HexagonPMUState *s)
{
    return s->thread_mask;
}

void hexagon_pmu_set_thread_mask(HexagonPMUState *s, uint32_t mask)
{
    s->thread_mask = mask;
}

bool hexagon_pmu_test_and_set_unimp_logged(HexagonPMUState *s, int event)
{
    g_assert(event >= 0 && event < PMU_NUM_EVENT_IDS);
    return test_and_set_bit(event, s->unimp_logged);
}

uint32_t hexagon_pmu_get_stid(HexagonPMUState *s, unsigned int index)
{
    g_assert(index < ARRAY_SIZE(s->stid));
    return s->stid[index];
}

void hexagon_pmu_set_stid(HexagonPMUState *s, unsigned int index,
                          uint32_t value)
{
    g_assert(index < ARRAY_SIZE(s->stid));
    s->stid[index] = value;
}

static void hexagon_pmu_reset_hold(Object *obj, ResetType type)
{
    HexagonPMUState *s = HEXAGON_PMU(obj);

    memset(s->events, 0, sizeof(s->events));
    memset(s->ctr_offset, 0, sizeof(s->ctr_offset));
    s->thread_mask = 0;
    memset(s->stid, 0, sizeof(s->stid));
}

static const VMStateDescription vmstate_hexagon_pmu = {
    .name = "hexagon_pmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_ARRAY(events, HexagonPMUState, NUM_PMU_CTRS),
        VMSTATE_UINT32_ARRAY(ctr_offset, HexagonPMUState, NUM_PMU_CTRS),
        VMSTATE_UINT32(thread_mask, HexagonPMUState),
        VMSTATE_UINT32_ARRAY(stid, HexagonPMUState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void hexagon_pmu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_pmu_reset_hold;
    dc->vmsd = &vmstate_hexagon_pmu;
    dc->user_creatable = false;
}

static const TypeInfo hexagon_pmu_info = {
    .name = TYPE_HEXAGON_PMU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonPMUState),
    .class_init = hexagon_pmu_class_init,
};

static void hexagon_pmu_register_types(void)
{
    type_register_static(&hexagon_pmu_info);
}

type_init(hexagon_pmu_register_types)
