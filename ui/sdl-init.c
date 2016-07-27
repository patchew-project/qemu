/*
 * QEMU SDL display driver init function
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

static void (*init_fn)(DisplayState *ds, int full_screen, int no_frame);
void sdl_register_init_fun(void *fn)
{
    assert(!init_fn);
    init_fn = fn;
}

bool sdl_display_early_init(int opengl)
{

#ifdef CONFIG_SDL2
    switch (opengl) {
    case -1: /* default */
    case 0:  /* off */
        break;
    case 1: /* on */
#ifdef CONFIG_OPENGL
        display_opengl = 1;
#endif
        break;
    default:
        g_assert_not_reached();
        break;
    }
#else
    if (opengl == 1 /* on */) {
        fprintf(stderr,
                "SDL1 display code has no opengl support.\n"
                "Please recompile qemu with SDL2, using\n"
                "./configure --enable-sdl --with-sdlabi=2.0\n");
        /* XXX: Should we return false here? */
    }
#endif

    module_call_init(MODULE_INIT_SDL);
    if (!init_fn) {
        return false;
    }
    return true;
}

void sdl_display_init(DisplayState *ds, int full_screen, int no_frame)
{
    assert(init_fn);
    init_fn(ds, full_screen, no_frame);
}
