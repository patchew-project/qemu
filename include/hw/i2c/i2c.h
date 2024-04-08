#ifndef QEMU_I2C_H
#define QEMU_I2C_H

#include "hw/qdev-core.h"
#include "qom/object.h"

/*
 * The QEMU I2C implementation only supports simple transfers that complete
 * immediately.  It does not support target devices that need to be able to
 * defer their response (eg. CPU target interfaces where the data is supplied
 * by the device driver in response to an interrupt).
 */

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_START_SEND_ASYNC,
    I2C_FINISH,
    I2C_NACK /* Masker NACKed a receive byte.  */
};

typedef struct I2CNodeList I2CNodeList;

#define TYPE_I2C_TARGET "i2c-slave"
OBJECT_DECLARE_TYPE(I2CTarget, I2CTargetClass,
                    I2C_TARGET)

struct I2CTargetClass {
    DeviceClass parent_class;

    /* Controller to target. Returns non-zero for a NAK, 0 for success. */
    int (*send)(I2CTarget *s, uint8_t data);

    /*
     * Controller to target (asynchronous).
     * Receiving target must call i2c_ack().
     */
    void (*send_async)(I2CTarget *s, uint8_t data);

    /*
     * Target to controller.  This cannot fail, the device should always
     * return something here.
     */
    uint8_t (*recv)(I2CTarget *s);

    /*
     * Notify the target of a bus state change.  For start event,
     * returns non-zero to NAK an operation.  For other events the
     * return code is not used and should be zero.
     */
    int (*event)(I2CTarget *s, enum i2c_event event);

    /*
     * Check if this device matches the address provided.  Returns bool of
     * true if it matches (or broadcast), and updates the device list, false
     * otherwise.
     *
     * If broadcast is true, match should add the device and return true.
     */
    bool (*match_and_add)(I2CTarget *candidate, uint8_t address, bool broadcast,
                          I2CNodeList *current_devs);
};

struct I2CTarget {
    DeviceState qdev;

    /* Remaining fields for internal use by the I2C code.  */
    uint8_t address;
};

#define TYPE_I2C_BUS "i2c-bus"
OBJECT_DECLARE_SIMPLE_TYPE(I2CBus, I2C_BUS)

typedef struct I2CNode I2CNode;

struct I2CNode {
    I2CTarget *elt;
    QLIST_ENTRY(I2CNode) next;
};

typedef struct I2CPendingController I2CPendingController;

struct I2CPendingController {
    QEMUBH *bh;
    QSIMPLEQ_ENTRY(I2CPendingController) entry;
};

typedef QLIST_HEAD(I2CNodeList, I2CNode) I2CNodeList;
typedef QSIMPLEQ_HEAD(I2CPendingControllers, I2CPendingController)
            I2CPendingControllers;

struct I2CBus {
    BusState qbus;
    I2CNodeList current_devs;
    I2CPendingControllers pending_controllers;
    uint8_t saved_address;
    bool broadcast;

    /* Set from target currently controlling the bus. */
    QEMUBH *bh;
};

I2CBus *i2c_init_bus(DeviceState *parent, const char *name);
int i2c_bus_busy(I2CBus *bus);

/**
 * i2c_start_transfer: start a transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the target
 * @is_recv: indicates the transfer direction
 *
 * When @is_recv is a known boolean constant, use the
 * i2c_start_recv() or i2c_start_send() helper instead.
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_transfer(I2CBus *bus, uint8_t address, bool is_recv);

/**
 * i2c_start_recv: start a 'receive' transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the target
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_recv(I2CBus *bus, uint8_t address);

/**
 * i2c_start_send: start a 'send' transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the target
 *
 * Returns: 0 on success, -1 on error
 */
int i2c_start_send(I2CBus *bus, uint8_t address);

/**
 * i2c_start_send_async: start an asynchronous 'send' transfer on an I2C bus.
 *
 * @bus: #I2CBus to be used
 * @address: address of the target
 *
 * Return: 0 on success, -1 on error
 */
int i2c_start_send_async(I2CBus *bus, uint8_t address);

void i2c_schedule_pending_controller(I2CBus *bus);

void i2c_end_transfer(I2CBus *bus);
void i2c_nack(I2CBus *bus);
void i2c_ack(I2CBus *bus);
void i2c_bus_controller(I2CBus *bus, QEMUBH *bh);
void i2c_bus_release(I2CBus *bus);
int i2c_send(I2CBus *bus, uint8_t data);
int i2c_send_async(I2CBus *bus, uint8_t data);
uint8_t i2c_recv(I2CBus *bus);
bool i2c_scan_bus(I2CBus *bus, uint8_t address, bool broadcast,
                  I2CNodeList *current_devs);

/**
 * Create an I2C target device on the heap.
 * @name: a device type name
 * @addr: I2C address of the target when put on a bus
 *
 * This only initializes the device state structure and allows
 * properties to be set. Type @name must exist. The device still
 * needs to be realized. See qdev-core.h.
 */
I2CTarget *i2c_target_new(const char *name, uint8_t addr);

/**
 * Create and realize an I2C target device on the heap.
 * @bus: I2C bus to put it on
 * @name: I2C target device type name
 * @addr: I2C address of the target when put on a bus
 *
 * Create the device state structure, initialize it, put it on the
 * specified @bus, and drop the reference to it (the device is realized).
 */
I2CTarget *i2c_target_create_simple(I2CBus *bus,
                                    const char *name, uint8_t addr);

/**
 * Realize and drop a reference an I2C target device
 * @dev: I2C target device to realize
 * @bus: I2C bus to put it on
 * @addr: I2C address of the target on the bus
 * @errp: pointer to NULL initialized error object
 *
 * Returns: %true on success, %false on failure.
 *
 * Call 'realize' on @dev, put it on the specified @bus, and drop the
 * reference to it.
 *
 * This function is useful if you have created @dev via qdev_new(),
 * i2c_target_new() or i2c_target_try_new() (which take a reference to
 * the device it returns to you), so that you can set properties on it
 * before realizing it. If you don't need to set properties then
 * i2c_target_create_simple() is probably better (as it does the create,
 * init and realize in one step).
 *
 * If you are embedding the I2C target into another QOM device and
 * initialized it via some variant on object_initialize_child() then
 * do not use this function, because that family of functions arrange
 * for the only reference to the child device to be held by the parent
 * via the child<> property, and so the reference-count-drop done here
 * would be incorrect.  (Instead you would want i2c_target_realize(),
 * which doesn't currently exist but would be trivial to create if we
 * had any code that wanted it.)
 */
bool i2c_target_realize_and_unref(I2CTarget *dev, I2CBus *bus, Error **errp);

/**
 * Set the I2C bus address of a target device
 * @dev: I2C target device
 * @address: I2C address of the target when put on a bus
 */
void i2c_target_set_address(I2CTarget *dev, uint8_t address);

extern const VMStateDescription vmstate_i2c_target;

#define VMSTATE_I2C_TARGET(_field, _state) {                          \
    .name       = (stringify(_field)),                                \
    .size       = sizeof(I2CTarget),                                  \
    .vmsd       = &vmstate_i2c_target,                                \
    .flags      = VMS_STRUCT,                                         \
    .offset     = vmstate_offset_value(_state, _field, I2CTarget),    \
}

#endif
