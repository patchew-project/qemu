/* Code to mangle pathnames into those matching a given prefix.
   eg. open("/lib/foo.so") => open("/usr/gnemul/i386-linux/lib/foo.so");

   The assumption is that this area does not change.
*/
#include "qemu/osdep.h"
#include <limits.h>
#include "qemu/cutils.h"
#include "qemu/path.h"

static const char *pathprefix;
int pathprefixfd = -1;
__thread char gluedpath[PATH_MAX];

void init_paths(const char *prefix)
{
    char pref_buf[PATH_MAX];

    if (prefix[0] == '\0' ||
        !strcmp(prefix, "/"))
        return;

    if (prefix[0] != '/') {
        char *cwd = getcwd(NULL, 0);
        size_t pref_buf_len = sizeof(pref_buf);

        if (!cwd)
            abort();
        pstrcpy(pref_buf, sizeof(pref_buf), cwd);
        pstrcat(pref_buf, pref_buf_len, "/");
        pstrcat(pref_buf, pref_buf_len, prefix);
        free(cwd);
        prefix = strdup(pref_buf);
        if (!prefix) {
            abort();
        }
    }

    pathprefix = prefix;
    pathprefixfd = open(pathprefix, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

/* Look for path in emulation dir, otherwise return name. */
const char *path(const char *name)
{
    const char *relname;
    /* Only do absolute paths: quick and dirty, but should mostly be OK.
       Could do relative by tracking cwd. */
    if ((pathprefixfd < 0) || !name || name[0] != '/') {
        return name;
    }

    relname = name + strspn(name, "/");
    if (faccessat(pathprefixfd, relname, R_OK, AT_EACCESS) == 0) {
        snprintf(gluedpath, sizeof(gluedpath), "%s%s", pathprefix, name);
        return gluedpath;
    }

    return name;
}
