#include "qemu/osdep.h"
#include "migration/cpr.h"

void cpr_save_fd(const char *name, int id, int fd)
{
}

void cpr_delete_fd(const char *name, int id)
{
}

int cpr_find_fd(const char *name, int id)
{
    return -1;
}
