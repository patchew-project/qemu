#include "sysemu/tpm.h"
#include "hw/platform-bus.h"
#include "hw/sysbus.h"
#include "qapi/error.h"

void tpm_sysbus_plug(TPMIf *tpmif, Object *pbus, hwaddr pbus_base)
{
    PlatformBusDevice *pbusdev = PLATFORM_BUS_DEVICE(pbus);
    SysBusDevice *sbdev = SYS_BUS_DEVICE(tpmif);
    MemoryRegion *sbdev_mr;
    hwaddr tpm_base;
    uint64_t tpm_size;

    /* exit early if TPM is not a sysbus device */
    if (!object_dynamic_cast(OBJECT(tpmif), TYPE_SYS_BUS_DEVICE)) {
        return;
    }

    assert(object_dynamic_cast(pbus, TYPE_PLATFORM_BUS_DEVICE));

    tpm_base = platform_bus_get_mmio_addr(pbusdev, sbdev, 0);
    assert(tpm_base != -1);

    tpm_base += pbus_base;

    sbdev_mr = sysbus_mmio_get_region(sbdev, 0);
    tpm_size = memory_region_size(sbdev_mr);

    object_property_set_uint(OBJECT(sbdev), "x-baseaddr",
                             tpm_base, &error_abort);
    object_property_set_uint(OBJECT(sbdev), "x-size",
                             tpm_size, &error_abort);
}
