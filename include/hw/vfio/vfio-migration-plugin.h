#ifndef HW_VFIO_PLUGIN_MIGRATION_H
#define HW_VFIO_PLUGIN_MIGRATION_H

#include <stdint.h>

#define VFIO_LM_PLUGIN_API_VERSION  0

typedef struct VFIOMigrationPluginOps {
    void *(*init)(char *devid, char *arg);
    int (*save)(void *handle, uint8_t *state, uint64_t len);
    int (*load)(void *handle, uint8_t *state, uint64_t len);
    int (*update_pending)(void *handle, uint64_t *pending_bytes);
    int (*set_state)(void *handle, uint32_t value);
    int (*get_state)(void *handle, uint32_t *value);
    int (*cleanup)(void *handle);
} VFIOMigrationPluginOps;

typedef int (*VFIOLMPluginGetVersion)(void);
typedef VFIOMigrationPluginOps* (*VFIOLMPluginGetOps)(void);

#endif
