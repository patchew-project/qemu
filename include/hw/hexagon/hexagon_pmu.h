/*
 * Hexagon PMU QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_PMU_H
#define HEXAGON_PMU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"
#include "target/hexagon/pmu.h"

#define TYPE_HEXAGON_PMU "hexagon-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonPMUState, HEXAGON_PMU)

struct HexagonPMUState {
    SysBusDevice parent_obj;

    /* Event selected for each of the NUM_PMU_CTRS counters. */
    uint16_t events[NUM_PMU_CTRS];

    /* Per-counter rebase offset; see hexagon_get_pmu_counter(). */
    uint32_t ctr_offset[NUM_PMU_CTRS];

    /* PMUCFG[20:16] thread mode; only mode 0 (no qualification) is used. */
    uint32_t thread_mask;

    /* Raw PMUSTID0-2; not yet implemented beyond being stored. */
    uint32_t stid[3];

    /* Events already reported as unimplemented; not migrated. */
    DECLARE_BITMAP(unimp_logged, PMU_NUM_EVENT_IDS);
};

uint16_t hexagon_pmu_get_event(HexagonPMUState *s, unsigned int index);
void hexagon_pmu_set_event(HexagonPMUState *s, unsigned int index,
                           uint16_t event);
uint32_t hexagon_pmu_get_offset(HexagonPMUState *s, unsigned int index);
void hexagon_pmu_set_offset(HexagonPMUState *s, unsigned int index,
                            uint32_t offset);
uint32_t hexagon_pmu_get_thread_mask(HexagonPMUState *s);
void hexagon_pmu_set_thread_mask(HexagonPMUState *s, uint32_t mask);
bool hexagon_pmu_test_and_set_unimp_logged(HexagonPMUState *s, int event);
uint32_t hexagon_pmu_get_stid(HexagonPMUState *s, unsigned int index);
void hexagon_pmu_set_stid(HexagonPMUState *s, unsigned int index,
                          uint32_t value);

#endif /* HEXAGON_PMU_H */
