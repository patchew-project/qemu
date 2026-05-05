/*
 * ASPEED Caliptra emulator backend
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_CALIPTRA_EMU_H
#define ASPEED_CALIPTRA_EMU_H

#define TYPE_ASPEED_CALIPTRA_EMU "aspeed-caliptra-emu"

/*
 * RFC/PoC integration hook only.
 *
 * This address is used by the current QEMU experiment to attach the
 * external Caliptra backend to the AST2700fc machine. It must not be
 * interpreted as the final functional interface used by real hardware:
 * early Caliptra interaction is bootmcu-owned, and CA35 does not use
 * this path for functional access to the Caliptra core.
 *
 * The 128 KiB region covers:
 *   +0x00000  mailbox CSR   (caliptra APB 0x30020000)
 *   +0x10000  SOC IFC       (caliptra APB 0x30030000)
 */
#define ASPEED_CALIPTRA_MMIO_BASE 0x14c60000ULL

#endif /* ASPEED_CALIPTRA_EMU_H */
