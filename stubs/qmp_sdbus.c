#include "qemu/osdep.h"
#include "qmp-commands.h"
#include "hw/sd/sd.h"

SDBusCommandResponse *qmp_x_debug_sdbus_command(const char *qom_path,
                                                uint8_t command,
                                                bool has_arg, uint64_t arg,
                                                bool has_crc, uint16_t crc,
                                                Error **errp)
{
    return NULL;
}
