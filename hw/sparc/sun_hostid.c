/*
 * Shared "hostid"/"machineid" -machine sub-property implementation.
 * See include/hw/sparc/sun_hostid.h for the design rationale.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/sparc/sun_hostid.h"

static void sun_hostid_get_hostid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    SunHostIDProps *p = sun_hostid_props(obj, (ptrdiff_t)(uintptr_t)opaque);
    uint32_t value = p->hostid;

    visit_type_uint32(v, name, &value, errp);
}

static void sun_hostid_set_hostid(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    SunHostIDProps *p = sun_hostid_props(obj, (ptrdiff_t)(uintptr_t)opaque);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value > 0xffffff) {
        error_setg(errp,
                   "hostid must be a 24-bit value (0x000000-0xffffff)");
        return;
    }
    p->hostid = value;
    p->hostid_set = true;
}

static void sun_hostid_get_machineid(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    SunHostIDProps *p = sun_hostid_props(obj, (ptrdiff_t)(uintptr_t)opaque);
    uint8_t value = p->machineid;

    visit_type_uint8(v, name, &value, errp);
}

static void sun_hostid_set_machineid(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    SunHostIDProps *p = sun_hostid_props(obj, (ptrdiff_t)(uintptr_t)opaque);
    uint8_t value;

    if (!visit_type_uint8(v, name, &value, errp)) {
        return;
    }
    p->machineid = value;
    p->machineid_set = true;
}

void sun_hostid_class_init(ObjectClass *oc, ptrdiff_t props_offset)
{
    void *opaque = (void *)(uintptr_t)props_offset;

    object_class_property_add(oc, "hostid", "uint32",
                              sun_hostid_get_hostid, sun_hostid_set_hostid,
                              NULL, opaque);
    object_class_property_set_description(oc, "hostid",
        "Override the 24-bit hostid stored in NVRAM/IDPROM "
        "(low 3 bytes of the classic Sun hostid; independent of "
        "the emulated NIC's MAC address)");

    object_class_property_add(oc, "machineid", "uint8",
                               sun_hostid_get_machineid,
                               sun_hostid_set_machineid, NULL, opaque);
    object_class_property_set_description(oc, "machineid",
        "Override the machine-type byte stored in NVRAM/IDPROM "
        "(top byte of the classic Sun hostid; normally fixed per "
        "machine model)");
}

void sun_hostid_instance_init(Object *obj, ptrdiff_t props_offset)
{
    SunHostIDProps *p = sun_hostid_props(obj, props_offset);

    p->hostid_set = false;
    p->machineid_set = false;
}
