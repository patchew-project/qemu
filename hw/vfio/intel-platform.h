/*
 * Device descriptions for Intel platforms.
 *
 * Copyright Intel Coporation 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HW_VFIO_INTEL_PLATFORM_H
#define HW_VFIO_INTEL_PLATFORM_H

#include "qemu/osdep.h"

enum intel_platform {
    INTEL_PLATFORM_UNINITIALIZED = 0,
    INTEL_SANDYBRIDGE,
    INTEL_IVYBRIDGE,
    INTEL_VALLEYVIEW,
    INTEL_HASWELL,
    INTEL_BROADWELL,
    INTEL_CHERRYVIEW,
    INTEL_SKYLAKE,
    INTEL_BROXTON,
    INTEL_MAX_PLATFORMS
};

struct intel_device_info {
    uint8_t gen;
    enum intel_platform platform;
    uint32_t gtt_entry_size;
    unsigned int (*get_stolen_size)(uint16_t gmch);
    unsigned int (*get_gtt_size)(uint16_t gmch);
};

const struct intel_device_info *intel_get_device_info(uint16_t device_id);

#endif /* HW_VFIO_INTEL_PLATFORM_H */
