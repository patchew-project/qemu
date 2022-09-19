/*
 * Internal memory management interfaces
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef EXEC_ADDRESS_SPACES_H
#define EXEC_ADDRESS_SPACES_H

/*
 * Internal interfaces between memory.c/exec.c/vl.c.  Do not #include unless
 * you're one of them.
 */

#include "exec/memory.h"

#ifndef CONFIG_USER_ONLY

/**
 * Get the root memory region.  This is a legacy function, provided for
 * compatibility. Prefer using SysBusState::system_memory directly.
 */
MemoryRegion *get_system_memory(void);

/**
 * Get the root I/O port region.  This is a legacy function, provided for
 * compatibility. Prefer using SysBusState::system_io directly.
 */
MemoryRegion *get_system_io(void);

/**
 * Get the root memory address space.  This is a legacy function, provided for
 * compatibility. Prefer using SysBusState::address_space_memory directly.
 */
AddressSpace *get_address_space_memory(void);

/**
 * Get the root I/O port address space.  This is a legacy function, provided
 * for compatibility. Prefer using SysBusState::address_space_io directly.
 */
AddressSpace *get_address_space_io(void);

#endif

#endif
