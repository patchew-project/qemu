/*
 * QEMU-specific types for instrumentation clients.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdlib.h>


struct QITraceEventIter {
    char buffer[(sizeof(size_t) * 2) + sizeof(char *)];
};
