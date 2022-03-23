/*
 * BlockBackend RAM Registrar
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_RAM_REGISTRAR_H
#define BLOCK_RAM_REGISTRAR_H

#include "exec/ramlist.h"

/**
 * struct BlockRAMRegistrar:
 *
 * Keeps RAMBlock memory registered with a BlockBackend using
 * blk_register_buf() including hotplugged memory.
 *
 * Emulated devices or other BlockBackend users initialize a BlockRAMRegistrar
 * with blk_ram_registrar_init() before submitting I/O requests with the
 * BLK_REQ_REGISTERED_BUF flag set.
 */
typedef struct {
    BlockBackend *blk;
    RAMBlockNotifier notifier;
} BlockRAMRegistrar;

void blk_ram_registrar_init(BlockRAMRegistrar *r, BlockBackend *blk);
void blk_ram_registrar_destroy(BlockRAMRegistrar *r);

#endif /* BLOCK_RAM_REGISTRAR_H */
