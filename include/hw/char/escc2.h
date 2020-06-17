/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Enhanced Serial Communication Controller (ESCC2 v3.2).
 * Modelled according to the user manual (version 07.96).
 *
 * Copyright (C) 2020 Jasper Lowell
 */
#ifndef HW_ESCC2_H
#define HW_ESCC2_H

#define TYPE_ESCC2      "ESCC2"
#define ESCC2(obj)      OBJECT_CHECK(ESCC2State, (obj), TYPE_ESCC2)

#define TYPE_ESCC2_ISA  "ESCC2_ISA"
#define ESCC2_ISA(obj)  OBJECT_CHECK(ESCC2ISAState, (obj), TYPE_ESCC2_ISA)

#endif /* HW_ESCC2_H */
