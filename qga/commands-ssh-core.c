#include "qemu/osdep.h"
#include <qga-qapi-types.h>
#include <stdbool.h>
#include "qapi/error.h"
#include "commands-ssh-core.h"

GStrv read_authkeys(const char *path, Error **errp)
{
    g_autoptr(GError) err = NULL;
    g_autofree char *contents = NULL;

    if (!g_file_get_contents(path, &contents, NULL, &err))
    {
        error_setg(errp, "failed to read '%s': %s", path, err->message);
        return NULL;
    }

    return g_strsplit(contents, "\n", -1);
}

bool check_openssh_pub_keys(strList *keys, size_t *nkeys, Error **errp)
{
    size_t n = 0;
    strList *k;

    for (k = keys; k != NULL; k = k->next)
    {
        if (!check_openssh_pub_key(k->value, errp))
        {
            return false;
        }
        n++;
    }

    if (nkeys)
    {
        *nkeys = n;
    }
    return true;
}

bool check_openssh_pub_key(const char *key, Error **errp)
{
    /* simple sanity-check, we may want more? */
    if (!key || key[0] == '#' || strchr(key, '\n'))
    {
        error_setg(errp, "invalid OpenSSH public key: '%s'", key);
        return false;
    }

    return true;
}