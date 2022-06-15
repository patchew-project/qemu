/*
 * QEMU firmware and keymap file search
 *
 * Copyright (c) 2003-2020 QEMU contributors
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
#include "qemu/datadir.h"
#include "qemu/cutils.h"
#include "trace.h"

/* User specified data directory */
static char *user_data_dir;

/* Extra build time defined search locations for firmware (NULL terminated) */
static char **extra_firmware_dirs;

/* Default built-in directories */
static char *default_data_dir;
static char *default_icon_dir;
static char *default_helper_dir;

/* Whether we're known to be executing from a build tree */
static bool in_build_dir;

char *qemu_find_file(int type, const char *name)
{
    const char *user_install_dir = NULL;
    char **extra_install_dirs = NULL;
    const char *rel_build_dir;
    const char *rel_install_dir;
    const char *default_install_dir;
    char *maybepath = NULL;
    size_t i;
    int ret;

    switch (type) {
    case QEMU_FILE_TYPE_BIOS:
        user_install_dir = user_data_dir;
        extra_install_dirs = extra_firmware_dirs;
        rel_install_dir = "";
        rel_build_dir = "pc-bios";
        default_install_dir = default_data_dir;
        break;

    case QEMU_FILE_TYPE_KEYMAP:
        user_install_dir = user_data_dir;
        rel_install_dir = "keymaps";
        rel_build_dir = "ui/keymaps";
        default_install_dir = default_data_dir;
        break;

    case QEMU_FILE_TYPE_ICON:
        rel_install_dir = "hicolor";
        rel_build_dir = "ui/icons";
        default_install_dir = default_icon_dir;
        break;

    case QEMU_FILE_TYPE_HELPER:
        rel_install_dir = "";
        rel_build_dir = "";
        default_install_dir = default_helper_dir;
        break;

    default:
        abort();
    }

#define TRY_LOAD(path)                                                  \
    do {                                                                \
        ret = access(path, R_OK);                                       \
        trace_datadir_load_file(name, path, ret == 0 ? 0 : errno);      \
        if (ret == 0) {                                                 \
            return maybepath;                                           \
        }                                                               \
        g_clear_pointer(&path, g_free);                                 \
    } while (0)

    if (user_install_dir) {
        maybepath = g_build_filename(user_install_dir, rel_install_dir,
                                     name, NULL);
        TRY_LOAD(maybepath);
    }

    if (in_build_dir) {
        maybepath = g_build_filename(qemu_get_exec_dir(), rel_build_dir,
                                     name, NULL);
    } else {
        if (extra_install_dirs) {
            for (i = 0; extra_install_dirs[i] != NULL; i++) {
                maybepath = g_build_filename(extra_install_dirs[i],
                                             name, NULL);
                TRY_LOAD(maybepath);
            }
        }

        maybepath = g_build_filename(default_install_dir, rel_install_dir,
                                     name, NULL);
    }
    TRY_LOAD(maybepath);

    return NULL;
}

void qemu_set_user_data_dir(const char *path)
{
    g_free(user_data_dir);
    user_data_dir = g_strdup(path);
}

void qemu_add_default_firmwarepath(void)
{
    size_t i;
    g_autofree char *builddir = NULL;

    builddir = g_build_filename(qemu_get_exec_dir(), "pc-bios", NULL);
    if (access(builddir, R_OK) == 0) {
        in_build_dir = true;
    }

    /* add configured firmware directories */
    extra_firmware_dirs = g_strsplit(CONFIG_QEMU_FIRMWAREPATH,
                                     G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; extra_firmware_dirs[i] != NULL ; i++) {
        g_autofree char *path = extra_firmware_dirs[i];
        extra_firmware_dirs[i] = get_relocated_path(path);
    }

    /* Add default dirs relative to the executable path */
    default_data_dir = get_relocated_path(CONFIG_QEMU_DATADIR);
    default_icon_dir = get_relocated_path(CONFIG_QEMU_ICONDIR);
    default_helper_dir = get_relocated_path(CONFIG_QEMU_HELPERDIR);

    trace_datadir_init(default_data_dir,
                       default_icon_dir,
                       default_helper_dir,
                       in_build_dir);
}

void qemu_list_data_dirs(void)
{
    int i;
    for (i = 0; extra_firmware_dirs[i] != NULL; i++) {
        printf("%s\n", extra_firmware_dirs[i]);
    }

    printf("%s\n", default_data_dir);
}
