/*
 * ASPEED AST2700 UFS Host Controller
 *
 * Copyright 2026 IBM Corp.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_UFS_H
#define ASPEED_UFS_H

#include "hw/ufs/ufs.h"

#define TYPE_ASPEED_UFS "aspeed-ufs"
#define ASPEED_UFS(obj) OBJECT_CHECK(UfsHc, (obj), TYPE_ASPEED_UFS)

#endif /* ASPEED_UFS_H */
