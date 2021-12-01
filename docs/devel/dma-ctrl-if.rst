DMA control interface
=====================

About the DMA control interface
-------------------------------

DMA engines embedded in peripherals can end up being controlled in
different ways on real hardware. One possible way is to allow software
drivers to access the DMA engine's register API and to allow the drivers
to configure and control DMA transfers through the API. A model of a DMA
engine in QEMU that is embedded and (re)used in this manner does not need
to implement the DMA control interface.

Another option on real hardware is to allow the peripheral embedding the
DMA engine to control the engine through a custom hardware DMA control
interface between the two. Software drivers in this scenario configure and
trigger DMA operations through the controlling peripheral's register API
(for example could writing a specific bit in a register propagate down to
a transfer start signal on the DMA control interface). At the same time
the status, result and interrupts for the transfer might still be intended
to be read and catched through the DMA engine's register API (and
signals).

::

    Hardware example
                   +------------+
                   |            |
                   | Peripheral |
                   |            |
                   +------------+
                        /\
                        ||   DMA control IF (custom)
                        \/
                   +------------+
                   | Peripheral |
                   |    DMA     |
                   +------------+

Figure 1. A peripheral controlling it's embedded DMA engine through a
custom DMA control interface

Above scenario can be modelled in QEMU by implementing this DMA control
interface in the DMA engine model. This will allow a peripheral embedding
the DMA engine to initiate DMA transfers through the engine using the
interface. At the same time the status, result and interrupts for the
transfer can be read and catched through the DMA engine's register API and
signals. An example implementation and usage of the DMA control interface
can be found in the Xilinx CSU DMA model and Xilinx Versal's OSPI model.

::

    Memory address
    (register API)
      0xf1010000   +---------+
                   |         |
                   | Versal  |
                   |  OSPI   |
                   |         |
                   +---------+
                       /\
                       ||  DMA control IF
                       \/
      0xf1011000   +---------+
                   |         |
                   | CSU DMA |
                   |  (src)  |
                   |         |
                   +---------+

Figure 2. Xilinx Versal's OSPI controls and initiates transfers on it's
CSU source DMA through a DMA control interface

DMA control interface files
---------------------------

``include/hw/dma/dma-ctrl-if.h``
``hw/dma/dma-ctrl-if.c``

DmaCtrlIfClass
--------------

The ``DmaCtrlIfClass`` contains the interface methods that can be
implemented by a DMA engine.

.. code-block:: c

    typedef struct DmaCtrlIfClass {
        InterfaceClass parent;

        /*
         * read: Start a read transfer on the DMA implementing the DMA control
         * interface
         *
         * @dma_ctrl: the DMA implementing this interface
         * @addr: the address to read
         * @len: the amount of bytes to read at 'addr'
         * @notify: the structure containg a callback to call and opaque pointer
         * to pass the callback when the transfer has been completed
         * @start_dma: true for starting the DMA transfer and false for just
         * refilling and proceding an already started transfer
         */
        void (*read)(DmaCtrlIf *dma, hwaddr addr, uint32_t len,
                     DmaCtrlIfNotify *notify, bool start_dma);
    } DmaCtrlIfClass;


DmaCtrlIfNotify
---------------

The ``DmaCtrlIfNotify`` contains a callback function that is called when a
transfer has been completed. It also contains an opaque pointer that is
passed in to the function as an argument.

.. code-block:: c

    typedef struct DmaCtrlIfNotify {
        void *opaque;
        dmactrlif_notify_fn cb;
    } DmaCtrlIfNotify;

dma_ctrl_if_read_with_notify
----------------------------

The ``dma_ctrl_if_read_with_notify`` function is used from a model
embedding the DMA engine for starting DMA read transfers.

.. code-block:: c

    /*
     * Start a read transfer on a DMA implementing the DMA control interface.
     * The DMA will notify the caller that 'len' bytes have been read at 'addr'
     * through the callback in the DmaCtrlIfNotify structure. For allowing refilling
     * an already started transfer the DMA notifies the caller before considering
     * the transfer done (e.g. before setting done flags, generating IRQs and
     * modifying other relevant internal device state).
     *
     * @dma_ctrl: the DMA implementing this interface
     * @addr: the address to read
     * @len: the amount of bytes to read at 'addr'
     * @notify: the structure containing a callback to call and opaque pointer
     * to pass the callback when the transfer has been completed
     * @start_dma: true for starting the DMA transfer and false for just
     * refilling and proceding an already started transfer
     */
    void dma_ctrl_if_read_with_notify(DmaCtrlIf *dma, hwaddr addr, uint32_t len,
                                      DmaCtrlIfNotify *notify, bool start_dma);

Example implementation of the DMA control interface
---------------------------------------------------

The example code below showing an implementation of the DMA control
interface is taken from the Xilinx CSU DMA model.

The DMA control interface related code in
``include/hw/dma/xlnx_csu_dma.h`` is found below. The header includes
``include/hw/dma/dma-ctrl-if.h`` and makes it possible to keep track of a
notifier function with a corresponding opaque. The notifier is called when
the transfer has been completed (with the opaque passed in as argument).

.. code-block:: c

    .
    .
    .
    #include "hw/dma/dma-ctrl-if.h"
    .
    .
    .
    typedef struct XlnxCSUDMA {
    .
    .
    .
        dmactrlif_notify_fn dmactrlif_notify;
        void *dmactrlif_opaque;
    .
    .
    .
    } XlnxCSUDMA;
    .
    .
    .

The DMA control interface related code inside ``hw/dma/xlnx_csu_dma.c`` is
shown below. A DMA control interface read function gets installed in the
class init function through which DMA read transfers can be started. The
DMA control interface notifier is called once a transfer has been
completed (with the opaque passed in as argument) but before any DMA
status has been updated (for allowing refilling the transfer).

.. code-block:: c

    .
    .
    .
    static uint32_t xlnx_csu_dma_advance(XlnxCSUDMA *s, uint32_t len)
    {
    .
    .
    .
        /* Notify dma-ctrl-if clients when the transfer has been completed */
        if (size == 0 && s->dmactrlif_notify) {
            s->dmactrlif_notify(s->dmactrlif_opaque);
        }

        if (size == 0) {
            xlnx_csu_dma_done(s);
        }

        return size;
    }
    .
    .
    .
    static void xlnx_csu_dma_dma_ctrl_if_read(DmaCtrlIf *dma, hwaddr addr,
                                              uint32_t len, DmaCtrlIfNotify *notify,
                                              bool start_dma)
    {
    .
    .
    .
    static void xlnx_csu_dma_class_init(ObjectClass *klass, void *data)
    {
        DeviceClass *dc = DEVICE_CLASS(klass);
        StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
        DmaCtrlIfClass *dcic = DMA_CTRL_IF_CLASS(klass);
    .
    .
    .
        dcic->read = xlnx_csu_dma_dma_ctrl_if_read;
    }
    .
    .
    .
    static const TypeInfo xlnx_csu_dma_info = {
    .
    .
    .
        .interfaces = (InterfaceInfo[]) {
            { TYPE_STREAM_SINK },
            { TYPE_DMA_CTRL_IF },
            { }
        }
    };

Example DMA control interface read transfer start
-------------------------------------------------

The DMA read transfer example is taken from the Xilinx Versal's OSPI
model. The DMA read transfer is started by a register write to the OSPI
controller.

The DMA control interface related code inside
``include/hw/ssi/xlnx-versal-ospi.h`` is shown below. The header includes
``include/hw/dma/dma-ctrl-if.h`` and the state structure contains a
pointer to a DMA engine that has implemented the DMA control interface.

.. code-block:: c

    .
    .
    .
    #include "hw/dma/dma-ctrl-if.h"
    .
    .
    .
    typedef struct XlnxVersalOspi {
    .
    .
    .
        DmaCtrlIf *dma_src;
    .
    .
    .
    } XlnxVersalOspi;
    .
    .
    .

The DMA control interface related code inside
``hw/ssi/xlnx-versal-ospi.c`` can be seen below. OSPI DMA read transfers
are performed and executed through the DMA control interface read function
(and with the CSU source DMA). The OSPI controller is also able to refill
the transfer as required through the notifier (which is called when the
transfer has been completed).

.. code-block:: c

    static void ospi_dma_read(XlnxVersalOspi *s, bool start_dma)
    {
    .
    .
    .
        DmaCtrlIfNotify notify = { .cb = ospi_notify,
                                   .opaque = (void *)s };
    .
    .
    .
            dma_ctrl_if_read_with_notify(s->dma_src, 0, dma_len,
                                         &notify, start_dma);
    .
    .
    .
    }
    .
    .
    .
    static void xlnx_versal_ospi_init(Object *obj)
    {
    .
    .
    .
        object_property_add_link(obj, "dma-src", TYPE_DMA_CTRL_IF,
                                 (Object **)&s->dma_src,
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_STRONG);
    .
    .
    .
    }
