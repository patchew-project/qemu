/* Code to mangle pathnames into those matching a given prefix.
   eg. open("/lib/foo.so") => open("/usr/gnemul/i386-linux/lib/foo.so");

   The assumption is that this area does not change.
*/
#include "qemu/osdep.h"
#include <sys/param.h>
#include <dirent.h>
#include "qemu/cutils.h"
#include "qemu/path.h"
#include "qemu/thread.h"

static const char *base;
static GHashTable *hash;
static QemuMutex lock;

void init_paths(const char *prefix)
{
    if (prefix[0] == '\0' || !strcmp(prefix, "/")) {
        return;
    }

#if GLIB_CHECK_VERSION(2, 58, 0)
    base = g_canonicalize_filename(prefix, NULL);
#else
    if (prefix[0] != '/') {
        char *cwd = g_get_current_dir();
        base = g_build_filename(cwd, prefix, NULL);
        g_free(cwd);
    } else {
        base = g_strdup(prefix);
    }
#endif

    hash = g_hash_table_new(g_str_hash, g_str_equal);
    qemu_mutex_init(&lock);
}

/* Look for path in emulation dir, otherwise return name. */
const char *path(const char *name)
{
    gpointer key, value;
    char *ret;

    /* Only do absolute paths: quick and dirty, but should mostly be OK.  */
    if (!base || !name || name[0] != '/') {
        return name;
    }

    qemu_mutex_lock(&lock);

    /* Have we looked up this file before?  */
    if (g_hash_table_lookup_extended(hash, name, &key, &value)) {
        ret = value ? value : name;
    } else {
        char *full_name, *save_name;

        save_name = g_strdup(name);
#if GLIB_CHECK_VERSION(2, 58, 0)
        full_name = g_canonicalize_filename(g_path_skip_root(name), base);
#else
        full_name = g_build_filename(base, name, NULL);
#endif

        /* Look for the path; record the result, pass or fail.  */
        if (access(full_name, F_OK) == 0) {
            /* Exists.  */
            g_hash_table_insert(hash, save_name, full_name);
            ret = full_name;
        } else {
            /* Does not exist.  */
            g_free(full_name);
            g_hash_table_insert(hash, save_name, NULL);
            ret = name;
        }
    }

    qemu_mutex_unlock(&lock);
    return ret;
}
