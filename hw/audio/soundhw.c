/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/help_option.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/audio/soundhw.h"

struct soundhw {
    const char *name;
    bool is_found;
};

static gint soundhw_list_compare(gconstpointer a, gconstpointer b)
{
    SoundHwCmdlineClass *sc_a = SOUNDHW_CMDLINE_CLASS(a);
    SoundHwCmdlineClass *sc_b = SOUNDHW_CMDLINE_CLASS(b);

    return strcmp(sc_a->cmdline_name, sc_b->cmdline_name);
}

static void soundhw_list_entry(gpointer data, gpointer user_data)
{
    SoundHwCmdlineClass *sc = SOUNDHW_CMDLINE_CLASS(data);
    DeviceClass *dc = DEVICE_CLASS(data);

    printf("%-11s %s\n", sc->cmdline_name, dc->desc);
}

static void soundhw_check_enable_entry(gpointer data, gpointer user_data)
{
    SoundHwCmdlineClass *sc = SOUNDHW_CMDLINE_CLASS(data);
    struct soundhw *d = user_data;

    if (g_str_equal(d->name, "all") || g_str_equal(d->name, sc->cmdline_name)) {
        sc->option_used = d->is_found = true;
    }
}

static void soundhw_list(GSList *list)
{
    if (!list) {
        printf("Machine has no user-selectable audio hardware "
                "(it may or may not have always-present audio hardware).\n");
        return;
    }
    list = g_slist_sort(list, soundhw_list_compare);
    printf("Valid sound card names (comma separated):\n");
    g_slist_foreach(list, soundhw_list_entry, NULL);
    printf("\n-soundhw all will enable all of the above\n");
}

void select_soundhw(const char *optarg)
{
    struct soundhw data;
    GSList *list;

    list = object_class_get_list(SOUNDHW_CMDLINE_INTERFACE, false);

    if (is_help_option(optarg)) {
        soundhw_list(list);
        exit(0);
    }

    if (strchr(optarg, ',')) {
        char **parts = g_strsplit(optarg, ",", 0);
        char **tmp;

        for (tmp = parts; tmp && *tmp; tmp++) {
            data = (struct soundhw){ .name = *tmp };
            g_slist_foreach(list, soundhw_check_enable_entry, &data);
            if (!data.is_found) {
                goto invalid_name;
            }
        }
        g_strfreev(parts);
    } else {
        data = (struct soundhw){ .name = optarg };
        g_slist_foreach(list, soundhw_check_enable_entry, &data);
        if (!data.is_found) {
            goto invalid_name;
        }
    }
    g_slist_free(list);
    return;

invalid_name:
    error_report("Unknown sound card name `%s'", data.name);
    soundhw_list(list);
    exit(1);
}

static void soundhw_create_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    SoundHwCmdlineClass *sc = SOUNDHW_CMDLINE_CLASS(oc);
    const char *typename = object_class_get_name(oc);
    BusState *bus;

    if (!sc->option_used) {
        return;
    }

    warn_report("'-soundhw %s' is deprecated, please use '-device %s' instead",
                sc->cmdline_name, typename);

    if (object_class_dynamic_cast(oc, TYPE_ISA_DEVICE)) {
        bus = (BusState *)object_resolve_path_type("", TYPE_ISA_BUS, NULL);
        if (!bus) {
            error_report("ISA bus not available for %s", sc->cmdline_name);
            exit(1);
        }
        isa_create_simple(ISA_BUS(bus), typename);
    }
    if (object_class_dynamic_cast(oc, TYPE_PCI_DEVICE)) {
        bus = (BusState *)object_resolve_path_type("", TYPE_PCI_BUS, NULL);
        if (!bus) {
            error_report("PCI bus not available for %s", sc->cmdline_name);
            exit(1);
        }
        pci_create_simple(PCI_BUS(bus), -1, typename);
    }
}

void soundhw_init(void)
{
    GSList *list;

    list = object_class_get_list(SOUNDHW_CMDLINE_INTERFACE, false);
    if (list) {
        g_slist_foreach(list, soundhw_create_entry, NULL);
        g_slist_free(list);
    }
}

static const TypeInfo soundhw_interface_info = {
    .name       = SOUNDHW_CMDLINE_INTERFACE,
    .parent     = TYPE_INTERFACE,
    .class_size = sizeof(SoundHwCmdlineClass),
};

static void soundhw_register_types(void)
{
    type_register_static(&soundhw_interface_info);
}

type_init(soundhw_register_types)
