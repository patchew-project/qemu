/*
 * Xen virtual IOMMU (virtual VT-D)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen_backend.h"

#define TYPE_XEN_VIOMMU_DEVICE "xen_viommu"
#define  XEN_VIOMMU_DEVICE(obj) \
    OBJECT_CHECK(XenVIOMMUState, (obj), TYPE_XEN_VIOMMU_DEVICE)

typedef struct XenVIOMMUState XenVIOMMUState;

struct XenVIOMMUState {
    DeviceState dev;
    uint32_t id;
    uint64_t cap;
    uint64_t base_addr;
};

static void xen_viommu_realize(DeviceState *dev, Error **errp)
{
    int rc;
    uint64_t cap;
    char *dom;
    char viommu_path[1024];
    XenVIOMMUState *s = XEN_VIOMMU_DEVICE(dev);

    s->id = -1;
    
    /* Read vIOMMU attributes from Xenstore. */
    dom = xs_get_domain_path(xenstore, xen_domid);
    snprintf(viommu_path, sizeof(viommu_path), "%s/viommu", dom);
    rc = xenstore_read_uint64(viommu_path, "base_addr", &s->base_addr);  
    if (rc) {
        error_report("Can't get base address of vIOMMU");
        exit(1);
    }

    rc = xenstore_read_uint64(viommu_path, "cap", &s->cap);
    if (rc) {
        error_report("Can't get capabilities of vIOMMU");
        exit(1);
    }

    rc = xc_viommu_query_cap(xen_xc, xen_domid, &cap);
    if (rc) {
        exit(1);
    }


    if ((cap & s->cap) != cap) {
        error_report("xen: Unsupported capability %lx", s->cap);
        exit(1);
    }

    rc = xc_viommu_create(xen_xc, xen_domid, s->base_addr, s->cap, &s->id);
    if (rc) {
        s->id = -1;
        error_report("xen: failed(%d) to create viommu ", rc);
        exit(1);
    }
}

static void xen_viommu_instance_finalize(Object *o)
{
    int rc;
    XenVIOMMUState *s = XEN_VIOMMU_DEVICE(o);

    if (s->id != -1) {
        rc = xc_viommu_destroy(xen_xc, xen_domid, s->id); 
        if (rc) {
            error_report("xen: failed(%d) to destroy viommu ", rc);
        }
    }
}

static void xen_viommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->hotpluggable = false;
    dc->realize = xen_viommu_realize;
}

static const TypeInfo xen_viommu_info = {
    .name              = TYPE_XEN_VIOMMU_DEVICE,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(XenVIOMMUState),
    .instance_finalize = xen_viommu_instance_finalize,
    .class_init        = xen_viommu_class_init,
};

static void xen_viommu_register_types(void)
{
    type_register_static(&xen_viommu_info); 
}

type_init(xen_viommu_register_types);
