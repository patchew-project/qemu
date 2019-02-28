/*
 * QEMU curses/ncurses display driver
 * 
 * Copyright (c) 2005 Andrzej Zaborowski  <balrog@zabor.org>
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

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#endif
#include <locale.h>

#include "qapi/error.h"
#include "qemu-common.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"

/* KEY_EVENT is defined in wincon.h and in curses.h. Avoid redefinition. */
#undef KEY_EVENT
#include <curses.h>
#undef KEY_EVENT

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

static DisplayChangeListener *dcl;
static console_ch_t screen[160 * 100];
static WINDOW *screenpad = NULL;
static int width, height, gwidth, gheight, invalidate;
static int px, py, sminx, sminy, smaxx, smaxy;

static const wchar_t vga_to_wchar[256] = {
    // 0x0_
    L' ', L'\u263A', L'\u263B', L'\u2665',
    L'\u2666', L'\u2663', L'\u2660', L'\u2022',
    L'\u25D8', L'\u25CB', L'\u25D9', L'\u2642',
    L'\u2640', L'\u266A', L'\u266B', L'\u263C',

    // 0x1_
    L'\u25BA', L'\u25C4', L'\u2195', L'\u203C',
    L'\u00B6', L'\u00A7', L'\u25AC', L'\u21A8',
    L'\u2191', L'\u2193', L'\u2192', L'\u2190',
    L'\u221F', L'\u2194', L'\u25B2', L'\u25BC',

    // 0x2_
    L' ', L'!', L'"', L'#', L'$', L'%', L'&', L'\'',
    L'(', L')', L'*', L'+', L',', L'-', L'.', L'/',

    // 0x3_
    L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
    L'8', L'9', L':', L';', L'<', L'=', L'>', L'?',

    // 0x4_
    L'@', L'A', L'B', L'C', L'D', L'E', L'F', L'G',
    L'H', L'I', L'J', L'K', L'L', L'M', L'N', L'O',

    // 0x5_
    L'P', L'Q', L'R', L'S', L'T', L'U', L'V', L'W',
    L'X', L'Y', L'Z', L'[', L'\\', L']', L'^', L'_',

    // 0x6_
    L'`', L'a', L'b', L'c', L'd', L'e', L'f', L'g',
    L'h', L'i', L'j', L'k', L'l', L'm', L'n', L'o',

    // 0x7_
    L'p', L'q', L'r', L's', L't', L'u', L'v', L'w',
    L'x', L'y', L'z', L'{', L'|', L'}', L'~', L'\u2302',

    // 0x8_
    L'\u00C7', L'\u00FC', L'\u00E9', L'\u00E2',
    L'\u00E4', L'\u00E0', L'\u00E5', L'\u00E7',
    L'\u00EA', L'\u00EB', L'\u00E8', L'\u00EF',
    L'\u00EE', L'\u00EC', L'\u00C4', L'\u00C5',

    // 0x9_
    L'\u00C9', L'\u00E6', L'\u00C6', L'\u00F4',
    L'\u00F6', L'\u00F2', L'\u00FB', L'\u00F9',
    L'\u00FF', L'\u00D6', L'\u00DC', L'\u00A2',
    L'\u00A3', L'\u00A5', L'\u20A7', L'\u0192',

    // 0xA_
    L'\u00E1', L'\u00ED', L'\u00F3', L'\u00FA',
    L'\u00F1', L'\u00D1', L'\u00AA', L'\u00BA',
    L'\u00BF', L'\u2310', L'\u00AC', L'\u00BD',
    L'\u00BC', L'\u00A1', L'\u00AB', L'\u00BB',

    // 0xB_
    L'\u2591', L'\u2592', L'\u2593', L'\u2502',
    L'\u2524', L'\u2561', L'\u2562', L'\u2556',
    L'\u2555', L'\u2563', L'\u2551', L'\u2557',
    L'\u255D', L'\u255C', L'\u255B', L'\u2510',

    // 0xC_
    L'\u2514', L'\u2534', L'\u252C', L'\u251C',
    L'\u2500', L'\u253C', L'\u255E', L'\u255F',
    L'\u255A', L'\u2554', L'\u2569', L'\u2566',
    L'\u2560', L'\u2550', L'\u256C', L'\u2567',

    // 0xD_
    L'\u2568', L'\u2564', L'\u2565', L'\u2559',
    L'\u2558', L'\u2552', L'\u2553', L'\u256B',
    L'\u256A', L'\u2518', L'\u250C', L'\u2588',
    L'\u2584', L'\u258C', L'\u2590', L'\u2580',

    // 0xE_
    L'\u03B1', L'\u00DF', L'\u0393', L'\u03C0',
    L'\u03A3', L'\u03C3', L'\u00B5', L'\u03C4',
    L'\u03A6', L'\u0398', L'\u03A9', L'\u03B4',
    L'\u221E', L'\u03C6', L'\u03B5', L'\u2229',

    // 0xF_
    L'\u2261', L'\u00B1', L'\u2265', L'\u2264',
    L'\u2320', L'\u2321', L'\u00F7', L'\u2248',
    L'\u00B0', L'\u2219', L'\u00B7', L'\u221A',
    L'\u207F', L'\u00B2', L'\u25A0', L'\u00A0'
};

static void curses_update(DisplayChangeListener *dcl,
                          int x, int y, int w, int h)
{
    console_ch_t *line;
    cchar_t curses_line[width];

    line = screen + y * width;
    for (h += y; y < h; y ++, line += width) {
        for (x = 0; x < width; x++) {
            curses_line[x].attr = line[x] & ~0xff;
            curses_line[x].chars[0] = vga_to_wchar[line[x] & 0xff];
            curses_line[x].chars[1] = L'\0';
        }
        mvwadd_wchnstr(screenpad, y, 0, curses_line, width);
    }

    pnoutrefresh(screenpad, py, px, sminy, sminx, smaxy - 1, smaxx - 1);
    refresh();
}

static void curses_calc_pad(void)
{
    if (qemu_console_is_fixedsize(NULL)) {
        width = gwidth;
        height = gheight;
    } else {
        width = COLS;
        height = LINES;
    }

    if (screenpad)
        delwin(screenpad);

    clear();
    refresh();

    screenpad = newpad(height, width);

    if (width > COLS) {
        px = (width - COLS) / 2;
        sminx = 0;
        smaxx = COLS;
    } else {
        px = 0;
        sminx = (COLS - width) / 2;
        smaxx = sminx + width;
    }

    if (height > LINES) {
        py = (height - LINES) / 2;
        sminy = 0;
        smaxy = LINES;
    } else {
        py = 0;
        sminy = (LINES - height) / 2;
        smaxy = sminy + height;
    }
}

static void curses_resize(DisplayChangeListener *dcl,
                          int width, int height)
{
    if (width == gwidth && height == gheight) {
        return;
    }

    gwidth = width;
    gheight = height;

    curses_calc_pad();
}

#if !defined(_WIN32) && defined(SIGWINCH) && defined(KEY_RESIZE)
static volatile sig_atomic_t got_sigwinch;
static void curses_winch_check(void)
{
    struct winsize {
        unsigned short ws_row;
        unsigned short ws_col;
        unsigned short ws_xpixel;   /* unused */
        unsigned short ws_ypixel;   /* unused */
    } ws;

    if (!got_sigwinch) {
        return;
    }
    got_sigwinch = false;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
        return;
    }

    resize_term(ws.ws_row, ws.ws_col);
    invalidate = 1;
}

static void curses_winch_handler(int signum)
{
    got_sigwinch = true;
}

static void curses_winch_init(void)
{
    struct sigaction old, winch = {
        .sa_handler  = curses_winch_handler,
    };
    sigaction(SIGWINCH, &winch, &old);
}
#else
static void curses_winch_check(void) {}
static void curses_winch_init(void) {}
#endif

static void curses_cursor_position(DisplayChangeListener *dcl,
                                   int x, int y)
{
    if (x >= 0) {
        x = sminx + x - px;
        y = sminy + y - py;

        if (x >= 0 && y >= 0 && x < COLS && y < LINES) {
            move(y, x);
            curs_set(1);
            /* it seems that curs_set(1) must always be called before
             * curs_set(2) for the latter to have effect */
            if (!qemu_console_is_graphic(NULL)) {
                curs_set(2);
            }
            return;
        }
    }

    curs_set(0);
}

/* generic keyboard conversion */

#include "curses_keys.h"

static kbd_layout_t *kbd_layout = NULL;

static void curses_refresh(DisplayChangeListener *dcl)
{
    int chr, keysym, keycode, keycode_alt;

    curses_winch_check();

    if (invalidate) {
        clear();
        refresh();
        curses_calc_pad();
        graphic_hw_invalidate(NULL);
        invalidate = 0;
    }

    graphic_hw_text_update(NULL, screen);

    while (1) {
        /* while there are any pending key strokes to process */
        chr = getch();

        if (chr == ERR)
            break;

#ifdef KEY_RESIZE
        /* this shouldn't occur when we use a custom SIGWINCH handler */
        if (chr == KEY_RESIZE) {
            clear();
            refresh();
            curses_calc_pad();
            curses_update(dcl, 0, 0, width, height);
            continue;
        }
#endif

        keycode = curses2keycode[chr];
        keycode_alt = 0;

        /* alt key */
        if (keycode == 1) {
            int nextchr = getch();

            if (nextchr != ERR) {
                chr = nextchr;
                keycode_alt = ALT;
                keycode = curses2keycode[chr];

                if (keycode != -1) {
                    keycode |= ALT;

                    /* process keys reserved for qemu */
                    if (keycode >= QEMU_KEY_CONSOLE0 &&
                            keycode < QEMU_KEY_CONSOLE0 + 9) {
                        erase();
                        wnoutrefresh(stdscr);
                        console_select(keycode - QEMU_KEY_CONSOLE0);

                        invalidate = 1;
                        continue;
                    }
                }
            }
        }

        if (kbd_layout) {
            keysym = -1;
            if (chr < CURSES_KEYS)
                keysym = curses2keysym[chr];

            if (keysym == -1) {
                if (chr < ' ') {
                    keysym = chr + '@';
                    if (keysym >= 'A' && keysym <= 'Z')
                        keysym += 'a' - 'A';
                    keysym |= KEYSYM_CNTRL;
                } else
                    keysym = chr;
            }

            keycode = keysym2scancode(kbd_layout, keysym & KEYSYM_MASK,
                                      NULL, false);
            if (keycode == 0)
                continue;

            keycode |= (keysym & ~KEYSYM_MASK) >> 16;
            keycode |= keycode_alt;
        }

        if (keycode == -1)
            continue;

        if (qemu_console_is_graphic(NULL)) {
            /* since terminals don't know about key press and release
             * events, we need to emit both for each key received */
            if (keycode & SHIFT) {
                qemu_input_event_send_key_number(NULL, SHIFT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & CNTRL) {
                qemu_input_event_send_key_number(NULL, CNTRL_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALT) {
                qemu_input_event_send_key_number(NULL, ALT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALTGR) {
                qemu_input_event_send_key_number(NULL, GREY | ALT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }

            qemu_input_event_send_key_number(NULL, keycode & KEY_MASK, true);
            qemu_input_event_send_key_delay(0);
            qemu_input_event_send_key_number(NULL, keycode & KEY_MASK, false);
            qemu_input_event_send_key_delay(0);

            if (keycode & ALTGR) {
                qemu_input_event_send_key_number(NULL, GREY | ALT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALT) {
                qemu_input_event_send_key_number(NULL, ALT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & CNTRL) {
                qemu_input_event_send_key_number(NULL, CNTRL_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & SHIFT) {
                qemu_input_event_send_key_number(NULL, SHIFT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
        } else {
            keysym = -1;
            if (chr < CURSES_KEYS) {
                keysym = curses2qemu[chr];
            }
            if (keysym == -1)
                keysym = chr;

            kbd_put_keysym(keysym);
        }
    }
}

static void curses_atexit(void)
{
    endwin();
}

static void curses_setup(void)
{
    int i, colour_default[8] = {
        [QEMU_COLOR_BLACK]   = COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = COLOR_BLUE,
        [QEMU_COLOR_GREEN]   = COLOR_GREEN,
        [QEMU_COLOR_CYAN]    = COLOR_CYAN,
        [QEMU_COLOR_RED]     = COLOR_RED,
        [QEMU_COLOR_MAGENTA] = COLOR_MAGENTA,
        [QEMU_COLOR_YELLOW]  = COLOR_YELLOW,
        [QEMU_COLOR_WHITE]   = COLOR_WHITE,
    };

    /* input as raw as possible, let everything be interpreted
     * by the guest system */
    setlocale(LC_ALL, "");
    initscr(); noecho(); intrflush(stdscr, FALSE);
    nodelay(stdscr, TRUE); nonl(); keypad(stdscr, TRUE);
    start_color(); raw(); scrollok(stdscr, FALSE);

    /* Make color pair to match color format (3bits bg:3bits fg) */
    for (i = 0; i < 64; i++) {
        init_pair(i, colour_default[i & 7], colour_default[i >> 3]);
    }
    /* Set default color for more than 64 for safety. */
    for (i = 64; i < COLOR_PAIRS; i++) {
        init_pair(i, COLOR_WHITE, COLOR_BLACK);
    }
}

static void curses_keyboard_setup(void)
{
#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout,
                                          &error_fatal);
    }
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name        = "curses",
    .dpy_text_update = curses_update,
    .dpy_text_resize = curses_resize,
    .dpy_refresh     = curses_refresh,
    .dpy_text_cursor = curses_cursor_position,
};

static void curses_display_init(DisplayState *ds, DisplayOptions *opts)
{
#ifndef _WIN32
    if (!isatty(1)) {
        fprintf(stderr, "We need a terminal output\n");
        exit(1);
    }
#endif

    curses_setup();
    curses_keyboard_setup();
    atexit(curses_atexit);

    curses_winch_init();

    dcl = g_new0(DisplayChangeListener, 1);
    dcl->ops = &dcl_ops;
    register_displaychangelistener(dcl);

    invalidate = 1;
}

static QemuDisplay qemu_display_curses = {
    .type       = DISPLAY_TYPE_CURSES,
    .init       = curses_display_init,
};

static void register_curses(void)
{
    qemu_display_register(&qemu_display_curses);
}

type_init(register_curses);
