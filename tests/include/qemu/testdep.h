/*
 * Common dependencies for QEMU tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_TESTDEP_H
#define QEMU_TESTDEP_H

#include <stdint.h>
#include "qemu/compiler.h"

#define g_assert_not_reached __builtin_trap

#endif
