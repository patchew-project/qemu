/*
 * g_autowipe implementation for crypto secret wiping
 *
 * Copyright (c) 2019 Red Hat, Inc.
 * Copyright (c) 2019 Maxim Levitsky
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#include <stddef.h>
#include <malloc.h>
#include <glib.h>


/*
 * based on
 * https://www.cryptologie.net/article/419/zeroing-memory-compiler-optimizations-and-memset_s/
 */

static inline void memerase(void *pointer, size_t size)
{
#ifdef __STDC_LIB_EXT1__
    memset_s(pointer, size, 0, size);
#else
    /*volatile used to force compiler to not optimize the code away*/
    volatile unsigned char *p = pointer;
    while (size--) {
        *p++ = 0;
    }
#endif
}

static void g_autoptr_cleanup_generic_wipe_gfree(void *p)
{
    void **pp = (void **)p;
    size_t size = malloc_usable_size(*pp);
    memerase(*pp, size);
    g_free(*pp);
}

#define g_autowipe _GLIB_CLEANUP(g_autoptr_cleanup_generic_wipe_gfree)
