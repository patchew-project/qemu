/*
 * Rockchip RK3588 HPTIMER
 *
 * Copyright (C) 2026 Process Mission
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_ROCKCHIP_HPTIMER_H
#define HW_TIMER_ROCKCHIP_HPTIMER_H

#include "qom/object.h"

#define TYPE_ROCKCHIP_HPTIMER "rockchip-hptimer"
OBJECT_DECLARE_SIMPLE_TYPE(RockchipHPTimerState, ROCKCHIP_HPTIMER)

#define ROCKCHIP_HPTIMER_CTRL   0x04
#define ROCKCHIP_HPTIMER_ENABLE (1U << 0)

#endif /* HW_TIMER_ROCKCHIP_HPTIMER_H */
