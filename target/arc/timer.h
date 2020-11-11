/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#ifndef __ARC_TIMER_H__
#define __ARC_TIMER_H__

void arc_initializeTIMER(ARCCPU *);
void arc_resetTIMER(ARCCPU *);

void aux_timer_set(const struct arc_aux_reg_detail *, uint32_t, void *);
uint32_t aux_timer_get(const struct arc_aux_reg_detail *, void *);

#endif
