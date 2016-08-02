/*
 * QEMU GTK display driver init function
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "qemu/module.h"

static void (*early_init_fn)(int opengl);
static void (*init_fn)(DisplayState *ds, bool full_screen, bool grab_on_hover);

void gtk_register_early_init_fun(void *fn)
{
    assert(!early_init_fn);
    early_init_fn = fn;
}

void gtk_register_init_fun(void *fn)
{
    assert(!init_fn);
    init_fn = fn;
}

bool gtk_mod_init(void)
{
    module_load_one("ui-", "gtk");
    if (!early_init_fn || !init_fn) {
        return false;
    }
    return true;
}

void early_gtk_display_init(int opengl)
{
    assert(early_init_fn);
    early_init_fn(opengl);
}

void gtk_display_init(DisplayState *ds, bool full_screen, bool grab_on_hover)
{
    assert(init_fn);
    init_fn(ds, full_screen, grab_on_hover);
}
