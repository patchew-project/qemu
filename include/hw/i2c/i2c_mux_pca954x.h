#ifndef QEMU_I2C_MUX_PCA954X
#define QEMU_I2C_MUX_PCA954X

#include "hw/qdev-core.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"

#define PCA9548_CHANNEL_COUNT 8
#define PCA9546_CHANNEL_COUNT 4

/* The i2c mux shares ownership of a bus child. */
typedef struct PcaMuxChild {
    I2CSlave *child;

    /* The channel on which this child lives. */
    uint8_t channel;

    QSLIST_ENTRY(PcaMuxChild) sibling;
} PcaMuxChild;

typedef struct Pca954xState {
    SMBusDevice parent;

    uint8_t control;

    /* The children this mux co-owns with its parent bus. */
    QSLIST_HEAD(, PcaMuxChild) children;

    /* The number of children per channel. */
    unsigned int count[PCA9548_CHANNEL_COUNT];
} Pca954xState;

typedef struct Pca954xClass {
    SMBusDeviceClass parent;

    /* The number of channels this mux has. */
    uint8_t nchans;
} Pca954xClass;

#define TYPE_PCA9546 "pca9546"
#define TYPE_PCA9548 "pca9548"

#define TYPE_PCA954X "pca954x"

#define PCA954X(obj) OBJECT_CHECK(Pca954xState, (obj), TYPE_PCA954X)
#define PCA954X_CLASS(klass)                                                   \
     OBJECT_CLASS_CHECK(Pca954xClass, (klass), TYPE_PCA954X)
#define PCA954X_GET_CLASS(obj)                                                 \
     OBJECT_GET_CLASS(Pca954xClass, (obj), TYPE_PCA954X)

/**
 * pca954x_add_child - Adds a child i2c device to the mux device on the
 * specified channel.
 * @mux - The i2c mux to control this child.
 * @channel - The channel of the i2c mux that gates this child.
 * @child - The i2c child device to add to the mux.
 */
int pca954x_add_child(I2CSlave *mux, uint8_t channel, I2CSlave *child);

#endif
