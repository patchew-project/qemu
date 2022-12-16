/*
 * tb-flush prototype for use by the rest of the system.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _TB_FLUSH_H_
#define _TB_FLUSH_H_

/*
 * The following tb helpers don't require the caller to have any
 * target specific knowledge (CPUState can be treated as an anonymous
 * pointer for most).
 */

void tb_flush(CPUState *cpu);

#endif /* _TB_FLUSH_H_ */
