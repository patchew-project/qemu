#include "qemu/osdep.h"
#include "hw/sysbus-fdt.h"

static GHashTable *fdt_aliases(void)
{
    static GHashTable *fdt_aliases_singleton;

    if (!fdt_aliases_singleton) {
        fdt_aliases_singleton = g_hash_table_new(g_str_hash, g_str_equal);
    }
    return fdt_aliases_singleton;
}

void type_register_fdt_alias(const char *name, const char *alias)
{
    g_hash_table_insert(fdt_aliases(), (gpointer)name, (gpointer)name);
    g_hash_table_insert(fdt_aliases(), (gpointer)alias, (gpointer)name);
}

void type_register_fdt_aliases(const char *name, const char **aliases)
{
    for (; *aliases; aliases++) {
        type_register_fdt_alias(name, *aliases);
    }
}

const char *type_resolve_fdt_alias(const char *alias)
{
    return g_hash_table_lookup(fdt_aliases(), alias);
}
