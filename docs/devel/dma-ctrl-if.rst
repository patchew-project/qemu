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
(for example, writing a specific bit in a register could propagate down to
a transfer start signal on the DMA control interface). At the same time
the status, result and interrupts for the transfer might still be intended
to be read and caught through the DMA engine's register API (and
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

Figure 1. A peripheral controlling its embedded DMA engine through a
custom DMA control interface

The above scenario can be modelled in QEMU by implementing this DMA control
interface in the DMA engine model. This will allow a peripheral embedding
the DMA engine to initiate DMA transfers through the engine using the
interface. At the same time the status, result and interrupts for the
transfer can be read and caught through the DMA engine's register API and
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

Figure 2. Xilinx Versal's OSPI controls and initiates transfers on its
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
         * read: Start a read transfer on the DMA engine implementing the DMA
         * control interface
         *
         * @dma_ctrl: the DMA engine implementing this interface
         * @addr: the address to read
         * @len: the number of bytes to read at 'addr'
         *
         * @return a MemTxResult indicating whether the operation succeeded ('len'
         * bytes were read) or failed.
         */
        MemTxResult (*read)(DmaCtrlIf *dma, hwaddr addr, uint32_t len);
    } DmaCtrlIfClass;


dma_ctrl_if_read
----------------------------

The ``dma_ctrl_if_read`` function is used from a model embedding the DMA engine
for starting DMA read transfers.

.. code-block:: c

    /*
     * Start a read transfer on a DMA engine implementing the DMA control
     * interface.
     *
     * @dma_ctrl: the DMA engine implementing this interface
     * @addr: the address to read
     * @len: the number of bytes to read at 'addr'
     *
     * @return a MemTxResult indicating whether the operation succeeded ('len'
     * bytes were read) or failed.
     */
    MemTxResult dma_ctrl_if_read(DmaCtrlIf *dma, hwaddr addr, uint32_t len);


Example implementation of the DMA control interface
---------------------------------------------------

The example code below showing an implementation of the DMA control
interface is taken from the Xilinx CSU DMA model.

The DMA control interface related code inside ``hw/dma/xlnx_csu_dma.c`` is
shown below. A DMA control interface read function gets installed in the
class init function through which DMA read transfers can be started.

.. code-block:: c

    .
    .
    .
    static MemTxResult xlnx_csu_dma_dma_ctrl_if_read(DmaCtrlIf *dma, hwaddr addr,
                                                     uint32_t len)
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
    struct XlnxVersalOspi {
    .
    .
    .
        DmaCtrlIf *dma_src;
    .
    .
    .
    };
    .
    .
    .

The DMA control interface related code inside
``hw/ssi/xlnx-versal-ospi.c`` can be seen below. OSPI DMA read transfers
are performed and executed through the DMA control interface read function
(and with the CSU source DMA).

.. code-block:: c

    static void ospi_dma_read(XlnxVersalOspi *s)
    {
    .
    .
    .
    .
    .
        if (dma_ctrl_if_read(s->dma_src, 0, dma_len) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "OSPI DMA configuration error\n");
        }
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
