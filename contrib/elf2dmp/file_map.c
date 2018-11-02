/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include <stdio.h>
#include <glib.h>

#include "err.h"
#include "file_map.h"

int file_map(const char *name, mapped_file *mf)
{
    GError *err = NULL;

    mf->gmf = g_mapped_file_new(name, TRUE, &err);
    if (err) {
        eprintf("Failed to map file \'%s\'\n", name);
        return 1;
    }
    mf->map = g_mapped_file_get_contents(mf->gmf);
    mf->size = g_mapped_file_get_length(mf->gmf);

    return 0;
}

void file_unmap(mapped_file *mf)
{
    g_mapped_file_unref(mf->gmf);
}
