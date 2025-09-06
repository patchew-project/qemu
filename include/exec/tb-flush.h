/*
 * tb-flush prototype for use by the rest of the system.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _TB_FLUSH_H_
#define _TB_FLUSH_H_

/**
 * tb_flush__exclusive() - flush all translation blocks
 *
 * Used to flush all the translation blocks in the system.
 * Sometimes it is simpler to flush everything than work out which
 * individual translations are now invalid and ensure they are
 * not called anymore.
 *
 * Must be called from an exclusive context, e.g. start_exclusive
 * or vm_stop.
 */
void tb_flush__exclusive(void);

void tcg_flush_jmp_cache(CPUState *cs);

#endif /* _TB_FLUSH_H_ */
