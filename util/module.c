/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#ifdef CONFIG_MODULES
#include <gmodule.h>
#endif
#include "qemu/queue.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#ifdef CONFIG_MODULE_UPGRADES
#include "qemu-version.h"
#endif
#include "trace.h"

#include "qapi/error.h"
#include "qapi/qapi-types-modules.h"
#include "qapi/qapi-visit-modules.h"
#include "qapi/qobject-input-visitor.h"

typedef struct ModuleEntry
{
    void (*init)(void);
    QTAILQ_ENTRY(ModuleEntry) node;
    module_init_type type;
} ModuleEntry;

typedef QTAILQ_HEAD(, ModuleEntry) ModuleTypeList;

static ModuleTypeList init_type_list[MODULE_INIT_MAX];
static bool modules_init_done[MODULE_INIT_MAX];

static ModuleTypeList dso_init_list;

static void init_lists(void)
{
    static int inited;
    int i;

    if (inited) {
        return;
    }

    for (i = 0; i < MODULE_INIT_MAX; i++) {
        QTAILQ_INIT(&init_type_list[i]);
    }

    QTAILQ_INIT(&dso_init_list);

    inited = 1;
}


static ModuleTypeList *find_type(module_init_type type)
{
    init_lists();

    return &init_type_list[type];
}

void register_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;
    ModuleTypeList *l;

    e = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    l = find_type(type);

    QTAILQ_INSERT_TAIL(l, e, node);
}

void register_dso_module_init(void (*fn)(void), module_init_type type)
{
    ModuleEntry *e;

    init_lists();

    e = g_malloc0(sizeof(*e));
    e->init = fn;
    e->type = type;

    QTAILQ_INSERT_TAIL(&dso_init_list, e, node);
}

void module_call_init(module_init_type type)
{
    ModuleTypeList *l;
    ModuleEntry *e;

    if (modules_init_done[type]) {
        return;
    }

    l = find_type(type);

    QTAILQ_FOREACH(e, l, node) {
        e->init();
    }

    modules_init_done[type] = true;
}

#ifdef CONFIG_MODULES

static Modules *modinfo;
static char *module_dirs[5];
static int module_ndirs;

static void module_load_path_init(void)
{
    const char *search_dir;

    if (module_ndirs) {
        return;
    }

    search_dir = getenv("QEMU_MODULE_DIR");
    if (search_dir != NULL) {
        module_dirs[module_ndirs++] = g_strdup_printf("%s", search_dir);
    }
    module_dirs[module_ndirs++] = get_relocated_path(CONFIG_QEMU_MODDIR);
    module_dirs[module_ndirs++] = g_strdup(qemu_get_exec_dir());

#ifdef CONFIG_MODULE_UPGRADES
    version_dir = g_strcanon(g_strdup(QEMU_PKGVERSION),
                             G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "+-.~",
                             '_');
    module_dirs[module_ndirs++] = g_strdup_printf("/var/run/qemu/%s", version_dir);
#endif

    assert(module_ndirs <= ARRAY_SIZE(module_dirs));
}

static void module_load_modinfo(void)
{
    char *file, *json;
    FILE *fp;
    int i, size;
    Visitor *v;
    Error *errp = NULL;

    if (modinfo) {
        return;
    }

    for (i = 0; i < module_ndirs; i++) {
        file = g_strdup_printf("%s/modinfo.json", module_dirs[i]);
        fp = fopen(file, "r");
        if (fp != NULL) {
            break;
        }
        g_free(file);
    }
    if (NULL == fp) {
        warn_report("No modinfo.json file found.");
        return;
    } else {
        trace_module_load_modinfo(file);
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    json = g_malloc0(size + 1);
    fread(json, size, 1, fp);
    json[size] = 0;
    fclose(fp);

    v = qobject_input_visitor_new_str(json, NULL, &errp);
    if (errp) {
        error_reportf_err(errp, "parse error (%s)", file);
        g_free(file);
        return;
    }
    visit_type_Modules(v, NULL, &modinfo, &errp);
    visit_free(v);
    g_free(file);
}

static int module_load_file(const char *fname, bool mayfail, bool export_symbols)
{
    GModule *g_module;
    void (*sym)(void);
    const char *dsosuf = CONFIG_HOST_DSOSUF;
    int len = strlen(fname);
    int suf_len = strlen(dsosuf);
    ModuleEntry *e, *next;
    int ret, flags;

    if (len <= suf_len || strcmp(&fname[len - suf_len], dsosuf)) {
        /* wrong suffix */
        ret = -EINVAL;
        goto out;
    }
    if (access(fname, F_OK)) {
        ret = -ENOENT;
        goto out;
    }

    assert(QTAILQ_EMPTY(&dso_init_list));

    flags = 0;
    if (!export_symbols) {
        flags |= G_MODULE_BIND_LOCAL;
    }
    g_module = g_module_open(fname, flags);
    if (!g_module) {
        if (!mayfail) {
            fprintf(stderr, "Failed to open module: %s\n",
                    g_module_error());
        }
        ret = -EINVAL;
        goto out;
    }
    if (!g_module_symbol(g_module, DSO_STAMP_FUN_STR, (gpointer *)&sym)) {
        fprintf(stderr, "Failed to initialize module: %s\n",
                fname);
        /* Print some info if this is a QEMU module (but from different build),
         * this will make debugging user problems easier. */
        if (g_module_symbol(g_module, "qemu_module_dummy", (gpointer *)&sym)) {
            fprintf(stderr,
                    "Note: only modules from the same build can be loaded.\n");
        }
        g_module_close(g_module);
        ret = -EINVAL;
    } else {
        QTAILQ_FOREACH(e, &dso_init_list, node) {
            e->init();
            register_module_init(e->init, e->type);
        }
        ret = 0;
    }

    QTAILQ_FOREACH_SAFE(e, &dso_init_list, node, next) {
        QTAILQ_REMOVE(&dso_init_list, e, node);
        g_free(e);
    }
out:
    return ret;
}
#endif

bool module_load_one(const char *prefix, const char *lib_name, bool mayfail)
{
    bool success = false;

#ifdef CONFIG_MODULES
    char *fname = NULL;
#ifdef CONFIG_MODULE_UPGRADES
    char *version_dir;
#endif
    char *module_name;
    int i = 0;
    int ret;
    bool export_symbols = false;
    static GHashTable *loaded_modules;
    ModuleInfoList *modlist;
    strList *sl;

    if (!g_module_supported()) {
        fprintf(stderr, "Module is not supported by system.\n");
        return false;
    }

    if (!loaded_modules) {
        loaded_modules = g_hash_table_new(g_str_hash, g_str_equal);
    }

    module_name = g_strdup_printf("%s%s", prefix, lib_name);

    if (g_hash_table_contains(loaded_modules, module_name)) {
        g_free(module_name);
        return true;
    }
    g_hash_table_add(loaded_modules, module_name);

    module_load_path_init();
    module_load_modinfo();

    for (modlist = modinfo->list; modlist != NULL; modlist = modlist->next) {
        if (modlist->value->has_deps) {
            if (strcmp(modlist->value->name, module_name) == 0) {
                /* we depend on other module(s) */
                for (sl = modlist->value->deps; sl != NULL; sl = sl->next) {
                    module_load_one("", sl->value, false);
                }
            } else {
                for (sl = modlist->value->deps; sl != NULL; sl = sl->next) {
                    if (strcmp(module_name, sl->value) == 0) {
                        /* another module depends on us */
                        export_symbols = true;
                    }
                }
            }
        }
    }

    for (i = 0; i < module_ndirs; i++) {
        fname = g_strdup_printf("%s/%s%s",
                module_dirs[i], module_name, CONFIG_HOST_DSOSUF);
        ret = module_load_file(fname, mayfail, export_symbols);
        g_free(fname);
        fname = NULL;
        /* Try loading until loaded a module file */
        if (!ret) {
            success = true;
            break;
        }
    }

    if (!success) {
        g_hash_table_remove(loaded_modules, module_name);
        g_free(module_name);
    }

#endif
    return success;
}

#ifdef CONFIG_MODULES

static bool module_loaded_qom_all;

void module_load_qom_one(const char *type)
{
    ModuleInfoList *modlist;
    strList *sl;

    if (!type) {
        return;
    }

    module_load_path_init();
    module_load_modinfo();

    for (modlist = modinfo->list; modlist != NULL; modlist = modlist->next) {
        if (!modlist->value->has_objs) {
            continue;
        }
        for (sl = modlist->value->objs; sl != NULL; sl = sl->next) {
            if (strcmp(type, sl->value) == 0) {
                module_load_one("", modlist->value->name, false);
            }
        }
    }
}

void module_load_qom_all(void)
{
    ModuleInfoList *modlist;

    if (module_loaded_qom_all) {
        return;
    }

    module_load_path_init();
    module_load_modinfo();

    for (modlist = modinfo->list; modlist != NULL; modlist = modlist->next) {
        if (!modlist->value->has_objs) {
            continue;
        }
        module_load_one("", modlist->value->name, false);
    }
    module_loaded_qom_all = true;
}

void qemu_load_module_for_opts(const char *group)
{
    ModuleInfoList *modlist;

    module_load_path_init();
    module_load_modinfo();

    for (modlist = modinfo->list; modlist != NULL; modlist = modlist->next) {
        if (!modlist->value->has_opts) {
            continue;
        }
        if (strcmp(modlist->value->opts, group) == 0) {
            module_load_one("", modlist->value->name, false);
        }
    }
}

#else

void qemu_load_module_for_opts(const char *group) {}
void module_load_qom_one(const char *type) {}
void module_load_qom_all(void) {}

#endif
