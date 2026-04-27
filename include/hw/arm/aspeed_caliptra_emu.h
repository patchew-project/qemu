/*
 * ASPEED Caliptra emulator backend
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_CALIPTRA_EMU_H
#define ASPEED_CALIPTRA_EMU_H

#include "chardev/char.h"

void aspeed_caliptra_emu_start(Chardev *console, const char *rom_path,
                               const char *firmware_path);

#endif /* ASPEED_CALIPTRA_EMU_H */
