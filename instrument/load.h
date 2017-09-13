/*
 * Interface for (un)loading instrumentation libraries.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


#ifndef INSTRUMENT_LOAD_H
#define INSTRUMENT_LOAD_H

#include "qemu/osdep.h"

#include "qapi-types.h"
#include "qemu/queue.h"
#include "qemu/thread.h"


/**
 * InstrLoadError:
 * @INSTR_LOAD_OK: Correctly loaded.
 * @INSTR_LOAD_ID_EXISTS: Tried to load an instrumentation libraries with an
 *     existing ID.
 * @INSTR_LOAD_TOO_MANY: Tried to load too many instrumentation libraries.
 * @INSTR_LOAD_ERROR: The library's main() function returned a non-zero value.
 * @INSTR_LOAD_DLERROR: Error with libdl (see dlerror).
 *
 * Error codes for instr_load().
 */
typedef enum {
    INSTR_LOAD_OK,
    INSTR_LOAD_ID_EXISTS,
    INSTR_LOAD_TOO_MANY,
    INSTR_LOAD_ERROR,
    INSTR_LOAD_DLERROR,
} InstrLoadError;

/**
 * InstrUnloadError:
 * @INSTR_UNLOAD_OK: Correctly unloaded.
 * @INSTR_UNLOAD_INVALID: Invalid handle.
 * @INSTR_UNLOAD_DLERROR: Error with libdl (see dlerror).
 *
 * Error codes for instr_unload().
 */
typedef enum {
    INSTR_UNLOAD_OK,
    INSTR_UNLOAD_INVALID,
    INSTR_UNLOAD_DLERROR,
} InstrUnloadError;

/**
 * instr_load:
 * @path: Path to the shared library to load.
 * @argc: Number of arguments passed to the initialization function of the
 *     library.
 * @argv: Arguments passed to the initialization function of the library.
 * @id: Instrumentation library id.
 *
 * Load a dynamic trace instrumentation library.
 *
 * Returns: Whether the library could be loaded.
 */
InstrLoadError instr_load(const char *path, int argc, const char **argv,
                          const char **id);

/**
 * instr_unload:
 * @id: Instrumentation library id passed to instr_load().
 *
 * Unload the given instrumentation library.
 *
 * Returns: Whether the library could be unloaded.
 */
InstrUnloadError instr_unload(const char *id);

/**
 * instr_unload_all:
 *
 * Unload all instrumentation libraries.
 *
 * Returns: Whether any library could not be unloaded.
 */
InstrUnloadError instr_unload_all(void);

#endif  /* INSTRUMENT_LOAD_H */
