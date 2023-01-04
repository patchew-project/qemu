/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CAN device - MCP25625 chip model
 *
 * Copyright (c) 2022 SiFive, Inc.
 */

#define TYPE_MCP25625 "mcp25625"

#define MCP25625(obj)  OBJECT_CHECK(MCP25625State, (obj), TYPE_MCP25625)
