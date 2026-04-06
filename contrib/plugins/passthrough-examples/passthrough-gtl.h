/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PASSTHROUGH_GTL_H
#define PASSTHROUGH_GTL_H

#include <stdint.h>

typedef void (*PassthroughThunkEntry)(void **args, void *ret);

uint64_t passthrough_load_htl(const char *library);
uint64_t passthrough_dlsym(uint64_t handle, const char *symbol);
void passthrough_invoke(PassthroughThunkEntry entry, void **args, void *ret);
void passthrough_close_htl(uint64_t handle);

#endif /* PASSTHROUGH_GTL_H */
