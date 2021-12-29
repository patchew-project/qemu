#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H

/* Devices attached directly to the main system bus.  */

#include "hw/qdev-core.h"
#include "exec/memory.h"
#include "qom/object.h"

#define QDEV_MAX_MMIO 32
#define QDEV_MAX_PIO 32

#define TYPE_SYSTEM_BUS "System"
DECLARE_INSTANCE_CHECKER(BusState, SYSTEM_BUS,
                         TYPE_SYSTEM_BUS)


#define TYPE_SYS_BUS_DEVICE "sys-bus-device"
OBJECT_DECLARE_TYPE(SysBusDevice, SysBusDeviceClass,
                    SYS_BUS_DEVICE)

/**
 * SysBusDeviceClass:
 *
 * SysBusDeviceClass is not overriding #DeviceClass.realize, so derived
 * classes overriding it are not required to invoke its implementation.
 */

#define SYSBUS_DEVICE_GPIO_IRQ "sysbus-irq"

struct SysBusDeviceClass {
    /*< private >*/
    DeviceClass parent_class;

    /*
     * Let the sysbus device format its own non-PIO, non-MMIO unit address.
     *
     * Sometimes a class of SysBusDevices has neither MMIO nor PIO resources,
     * yet instances of it would like to distinguish themselves, in
     * OpenFirmware device paths, from other instances of the same class on the
     * sysbus. For that end we expose this callback.
     *
     * The implementation is not supposed to change *@dev, or incur other
     * observable change.
     *
     * The function returns a dynamically allocated string. On error, NULL
     * should be returned; the unit address portion of the OFW node will be
     * omitted then. (This is not considered a fatal error.)
     */
    char *(*explicit_ofw_unit_address)(const SysBusDevice *dev);
    void (*connect_irq_notifier)(SysBusDevice *dev, qemu_irq irq);
};

struct SysBusDevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int num_mmio;
    struct {
        hwaddr addr;
        MemoryRegion *memory;
    } mmio[QDEV_MAX_MMIO];
    int num_pio;
    uint32_t pio[QDEV_MAX_PIO];
};

typedef void FindSysbusDeviceFunc(SysBusDevice *sbdev, void *opaque);

void sysbus_init_mmio(SysBusDevice *dev, MemoryRegion *memory);
MemoryRegion *sysbus_mmio_get_region(SysBusDevice *dev, int n);

/**
 * sysbus_init_irq: Create an output GPIO line
 * @dev: Sysbus device to create output GPIO for
 * @irq: Pointer to qemu_irq for the GPIO lines
 *
 * Sysbus devices should use this function in their instance_init
 * or realize methods to create any output GPIO lines they need.
 *
 * The @irq argument should be a pointer to either a "qemu_irq" in
 * the device's state structure. The device implementation can then raise
 * and lower the GPIO line by calling qemu_set_irq(). (If anything is
 * connected to the other end of the GPIO this will cause the handler
 * function for that input GPIO to be called.)
 *
 * See sysbus_connect_irq() for how code that uses such a device can
 * connect to one of its output GPIO lines.
 *
 * There is no need to release the @pins allocated array because it
 * will be automatically released when @dev calls its instance_finalize()
 * handler.
 */
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *irq);

/**
 * sysbus_pass_irq: Create GPIO lines on container which pass through
 *                  to a target device
 * @dev: Device which needs to expose GPIO lines
 * @target: Device which has GPIO lines
 *
 * This function allows a container device to create GPIO arrays on itself
 * which simply pass through to a GPIO array of another device. It is
 * useful when modelling complex devices such system-on-chip, where a
 * sysbus device contains other sysbus devices.
 *
 * It is not possible to pass a subset of the GPIO lines with this function.
 *
 * To users of the container sysbus device, the GPIO array created on @dev
 * behaves exactly like any other.
 */
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);

void sysbus_init_ioports(SysBusDevice *dev, uint32_t ioport, uint32_t size);

bool sysbus_has_irq(SysBusDevice *dev, int n);
bool sysbus_has_mmio(SysBusDevice *dev, unsigned int n);

/**
 * sysbus_connect_irq: Connect a sysbus device output GPIO line
 * @dev: sysbus device whose GPIO to connect
 * @n: Number of the output GPIO line (which must be in range)
 * @pin: qemu_irq to connect the output line to
 *
 * This function connects an output GPIO line on a sysbus device
 * up to an arbitrary qemu_irq, so that when the device asserts that
 * output GPIO line, the qemu_irq's callback is invoked.
 * The index @n of the GPIO line must be valid, otherwise this function
 * will assert().
 *
 * Outbound GPIO lines can be connected to any qemu_irq, but the common
 * case is connecting them to another device's inbound GPIO line, using
 * the qemu_irq returned by qdev_get_gpio_in() or qdev_get_gpio_in_named().
 *
 * It is not valid to try to connect one outbound GPIO to multiple
 * qemu_irqs at once, or to connect multiple outbound GPIOs to the
 * same qemu_irq; see qdev_connect_gpio_out() for details.
 */
void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);

void sysbus_mmio_map(SysBusDevice *dev, int n, hwaddr addr);
void sysbus_mmio_map_overlap(SysBusDevice *dev, int n, hwaddr addr,
                             int priority);
void sysbus_mmio_unmap(SysBusDevice *dev, int n);
void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                   MemoryRegion *mem);
MemoryRegion *sysbus_address_space(SysBusDevice *dev);

bool sysbus_realize(SysBusDevice *dev, Error **errp);
bool sysbus_realize_and_unref(SysBusDevice *dev, Error **errp);

/* Call func for every dynamically created sysbus device in the system */
void foreach_dynamic_sysbus_device(FindSysbusDeviceFunc *func, void *opaque);

/* Legacy helper function for creating devices.  */
DeviceState *sysbus_create_varargs(const char *name,
                                 hwaddr addr, ...);

static inline DeviceState *sysbus_create_simple(const char *name,
                                              hwaddr addr,
                                              qemu_irq irq)
{
    return sysbus_create_varargs(name, addr, irq, NULL);
}

#endif /* HW_SYSBUS_H */
