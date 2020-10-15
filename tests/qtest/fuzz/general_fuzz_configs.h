/*
 * General Virtual-Device Fuzzing Target Configs
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef GENERAL_FUZZ_CONFIGS_H
#define GENERAL_FUZZ_CONFIGS_H

#include "qemu/osdep.h"

typedef struct general_fuzz_config {
    const char *name, *args, *objects;
} general_fuzz_config;

GArray *get_general_fuzz_configs(void);

#endif
