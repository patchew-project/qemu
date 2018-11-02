/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef FILE_MAP_H
#define FILE_MAP_H

#include <stdio.h>
#include <glib.h>

typedef struct mapped_file {
    GMappedFile *gmf;
    void *map;
    size_t size;
} mapped_file;

int file_map(const char *name, mapped_file *mf);
void file_unmap(mapped_file *mf);

#endif /* FILE_MAP_H */
