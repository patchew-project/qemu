#include "qemu/osdep.h"
#include "hw/qdev-core.h"

Object *do_qdev_get_machine(void)
{
    /*
     * This will create a "container" and add it to the QOM tree, if there
     * isn't one already.
     */
    return container_get(object_get_root(), "/machine");
}
