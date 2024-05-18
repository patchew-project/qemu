/*
 * EIF (Enclave Image Format) related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef HW_I386_EIF_H
#define HW_I386_EIF_H

bool read_eif_file(const char *eif_path, char **kernel_path, char **initrd_path,
                    char **kernel_cmdline, Error **errp);

bool check_if_eif_file(const char *path, bool *is_eif, Error **errp);

#endif

