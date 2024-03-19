#include <glib/gstrfuncs.h>
#include "qemu/osdep.h"

GStrv read_authkeys(const char *path, Error **errp);
bool check_openssh_pub_keys(strList *keys, size_t *nkeys, Error **errp);
bool check_openssh_pub_key(const char *key, Error **errp);

typedef struct WindowsUserInfo
{
    char *sshDirectory;
    char *authorizedKeyFile;
    char *username;
    char *SSID;
    bool isAdmin;
} WindowsUserInfo;

typedef WindowsUserInfo *PWindowsUserInfo;

void free_userInfo(PWindowsUserInfo info);
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(PWindowsUserInfo, free_userInfo, NULL);