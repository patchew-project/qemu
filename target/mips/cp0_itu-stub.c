/*
 * QEMU Inter-Thread Communication Unit emulation stubs
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/misc/mips_itu.h"

void itc_reconfigure(MIPSITUState *tag)
{
    /* nothing? */
}
