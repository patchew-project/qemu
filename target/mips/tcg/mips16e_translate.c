/*
 * MIPS emulation for QEMU - MIPS16e translation routines
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "translate.h"

/* Include the auto-generated decoders.  */
#include "decode-mips16e_16.c.inc"
#include "decode-mips16e_32.c.inc"
