/*
 * Shared "hostid"/"machineid" -machine sub-property implementation,
 * used by both hw/sparc/sun4m.c (sun4m-common) and hw/sparc64/sun4u.c
 * (sun4u-common) to represent the classic Sun "hostid" (as
 * reported by hostid(1) and used by some software for license
 * checks.
 *
 * The value is split across the emulated NVRAM/IDPROM into a fixed
 * per-model machine-type byte and the low 24 bits of the Ethernet
 * MAC address. These values can be changed in the NVRAM.
 * For QEMU, each machine family embeds a SunHostIDProps member
 * anywhere convenient in its own instance struct and wires it up
 * from instance_init/class_init via the two functions below, passing
 * the byte offset of that member (offsetof(...)) so the shared
 * getter/setters can find it regardless of which struct they're in.
 */
#ifndef HW_SPARC_SUN_HOSTID_H
#define HW_SPARC_SUN_HOSTID_H

#include "qom/object.h"

typedef struct SunHostIDProps {
    uint32_t hostid;      /* low 24 bits valid when hostid_set */
    uint8_t machineid;    /* valid when machineid_set */
    bool hostid_set;
    bool machineid_set;
} SunHostIDProps;

/*
 * Register the "hostid" and "machineid" properties on class oc.
 * props_offset is offsetof(YourMachineState, <SunHostIDProps field>).
 */
void sun_hostid_class_init(ObjectClass *oc, ptrdiff_t props_offset);

/*
 * Initialise the *_set flags to false for a freshly created instance.
 * Call from your machine's instance_init with the same offset.
 */
void sun_hostid_instance_init(Object *obj, ptrdiff_t props_offset);

/* Convenience accessor: fetch the SunHostIDProps embedded at offset. */
static inline SunHostIDProps *sun_hostid_props(Object *obj,
                                               ptrdiff_t props_offset)
{
    return (SunHostIDProps *)((char *)obj + props_offset);
}

#endif /* HW_SPARC_SUN_HOSTID_H */
