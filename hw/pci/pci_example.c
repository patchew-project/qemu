#include "qemu/osdep.h"
#include "hw/pci/pci.h"

/* this will be the name of the device from qemu point of view */
#define TYPE_PCI_EXAMPLE "pci-example"

/* each device should have a macro to be cast to using OBJECT_CHECK macro */
#define PCI_EXAMPLE_DEVICE(obj)  \
    OBJECT_CHECK(PCIExampleDevice, (obj), TYPE_PCI_EXAMPLE)

#define EXAMPLE_MMIO_SIZE 8
#define EXAMPLE_PIO_SIZE 8
#define DMA_BUF_SIZE 4096

/*---------------------------------------------------------------------------*/
/*                                 PCI Struct                                */
/*---------------------------------------------------------------------------*/

typedef struct PCIExampleDevice {
    /*< private >*/
    /* this device inherits from PCIDevice according to QEMU Object Model */
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion portio;
    MemoryRegion mmio;
    MemoryRegion irqio;
    MemoryRegion dmaio;

    /*
     * data registers:
     * mem_data, the register that holds the data on MMIO
     * pio_data, the register that holds the data on PORTIO
     * dma_physical_base, the register that holds the address of the DMA buffer
     */
    uint64_t mem_data, io_data, dma_physical_base;

    qemu_irq irq;
    /* for the driver to determine if this device caused the interrupt */
    uint64_t threw_irq;

} PCIExampleDevice;


/*---------------------------------------------------------------------------*/
/*                         Read/Write functions                              */
/*---------------------------------------------------------------------------*/

/*
 * do nothing because the mmio read is done from DMA buffer
 * this function shouldn't be called.
 * another option is to assign pci_example_mmio_ops.read = NULL.
 */
static uint64_t pci_example_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    assert(0);
    return 0;
}

static void pci_example_mmio_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);
    PCIDevice *pd = PCI_DEVICE(opaque); /* FIXME: refer to ped->parent_obj ? */

    /* driver uses iowrite8() so it's guarantee that only 1 byte is written */
    assert(size == 1);

    /* compute the result */
    ped->mem_data = val * 2;

    /* write the result directly to physical memory */
    cpu_physical_memory_write(ped->dma_physical_base, &ped->mem_data,
            DMA_BUF_SIZE);

    /* raise an IRQ to notify DMA has finished  */
    ped->threw_irq = 1;
    pci_irq_assert(pd);
}

static uint64_t pci_example_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);

    /* driver uses ioread8() so it's guarantee that only 1 byte is read */
    assert(size == 1);

    return ped->io_data;
}

static void pci_example_pio_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);
    PCIDevice *pd = PCI_DEVICE(opaque);

    /* driver uses iowrite8() so it's guarantee that only 1 byte is written */
    assert(size == 1);

    ped->io_data = val * 2;
    ped->threw_irq = 1;
    pci_irq_assert(pd);
}

static uint64_t pci_example_irqio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);

    /* driver uses iowrite8() so it's guarantee that only 1 byte is written */
    assert(size == 1);

    return ped->threw_irq;
}

static void pci_example_irqio_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);
    PCIDevice *pd = PCI_DEVICE(opaque);

    /* driver uses iowrite8() so it's guarantee that only 1 byte is written */
    assert(size == 1);

    /* give the ability to assert IRQ , we will use it only to deassert IRQ */
    if (val) {
        pci_irq_assert(pd);
        ped->threw_irq = 1;
    } else {
        ped->threw_irq = 0;
        pci_irq_deassert(pd);
    }
}

/*
 * do nothing because physical DMA buffer address is only set and doesn't need
 * to be read.
 * this function shouldn't be called.
 * another option is to assign pci_example_dma_ops.read = NULL.
 */
static uint64_t pci_example_dma_base_read(void *opaque, hwaddr addr,
        unsigned size)
{
    assert(0);
    return 0;
}

static void pci_example_dma_base_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(opaque);

    assert(size == 4);

    /*
     * notify the device about the physical address of the DMA buffer that the
     * driver has allocated
     */
    switch (addr) {
        /* lower bytes */
        case(0):
            ped->dma_physical_base &= 0xffffffff00000000;
            break;

        /* upper bytes */
        case(4):
            val <<= 32;
            ped->dma_physical_base &= 0x00000000ffffffff;
            break;
    }

    ped->dma_physical_base |= val;
}

/*---------------------------------------------------------------------------*/
/*                             PCI region ops                                */
/*---------------------------------------------------------------------------*/

/* callback called when memory region representing the MMIO space is accessed */
static const MemoryRegionOps pci_example_mmio_ops = {
    .read = pci_example_mmio_read,
    .write = pci_example_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* callback called when memory region representing the PIO space is accessed */
static const MemoryRegionOps pci_example_pio_ops = {
    .read = pci_example_pio_read,
    .write = pci_example_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* callback called when memory region representing the IRQ space is accessed */
static const MemoryRegionOps pci_example_irqio_ops = {
    .read = pci_example_irqio_read,
    .write = pci_example_irqio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* callback called when memory region representing the DMA space is accessed */
static const MemoryRegionOps pci_example_dma_ops = {
    .read = pci_example_dma_base_read,
    .write = pci_example_dma_base_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*---------------------------------------------------------------------------*/
/*                             PCI functions                                 */
/*---------------------------------------------------------------------------*/

/*
 * this is called when the device is initialized via launching the vm with
 * "-device <device name>" or via hotplug.
 */
static void pci_example_realize(PCIDevice *pd, Error **errp)
{
   PCIExampleDevice *ped = PCI_EXAMPLE_DEVICE(pd);
   uint8_t *pci_conf = pd->config;

   /* initialize the memory region of the device */
   memory_region_init_io(&ped->mmio, OBJECT(ped), &pci_example_mmio_ops, ped,
           "pci-example-mmio", EXAMPLE_MMIO_SIZE);

   memory_region_init_io(&ped->portio, OBJECT(ped), &pci_example_pio_ops, ped,
           "pci-example-portio", EXAMPLE_PIO_SIZE);

   memory_region_init_io(&ped->irqio, OBJECT(ped), &pci_example_irqio_ops, ped,
           "pci-example-irqio", EXAMPLE_PIO_SIZE);

   memory_region_init_io(&ped->dmaio, OBJECT(ped), &pci_example_dma_ops, ped,
           "pci-example-dma-base", EXAMPLE_MMIO_SIZE);

   /* allocate BARs */
   pci_register_bar(pd, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &ped->mmio);
   pci_register_bar(pd, 1, PCI_BASE_ADDRESS_SPACE_IO, &ped->portio);
   pci_register_bar(pd, 2, PCI_BASE_ADDRESS_SPACE_IO, &ped->irqio);
   pci_register_bar(pd, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &ped->dmaio);

   /*
    * give interrupt support.
    * a pci device has 4 pin for interrupt, here we use pin A
    */
   pci_config_set_interrupt_pin(pci_conf, 1);
}


/* the destructor of pci_example_realize() */
static void pci_example_exit(PCIDevice *dev)
{
    /* unregister BARs and other stuff */
    /* FIXME: implement */
}


/* class constructor */
static void pci_example_class_init(ObjectClass *klass, void *data)
{
   /* sort of dynamic cast */
   DeviceClass *dc = DEVICE_CLASS(klass);
   PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

   k->realize = pci_example_realize;
   k->exit = pci_example_exit;

   /* some regular IDs in HEXADECIMAL BASE */
   k->vendor_id = PCI_VENDOR_ID_REDHAT;
   k->device_id = PCI_DEVICE_ID_REDHAT_TEST;
   k->class_id = PCI_CLASS_OTHERS;

   /* set the device bitmap category */
   set_bit(DEVICE_CATEGORY_MISC, dc->categories);

   k->revision = 0x00;
   dc->desc = "PCI Example Device";
}

/*---------------------------------------------------------------------------*/
/*                            QEMU overhead                                  */
/*---------------------------------------------------------------------------*/


/*
 * Contains all the informations of the device we are creating.
 * class_init will be called when we are defining our device.
 */
static const TypeInfo pci_example_info = {
    .name           = TYPE_PCI_EXAMPLE,
    .parent         = TYPE_PCI_DEVICE,
    .instance_size  = sizeof(PCIExampleDevice),
    .class_init     = pci_example_class_init,
    .interfaces     = (InterfaceInfo[]) {
        /*
         * devices implementing this interface can be plugged to PCI bus.
         * for PCIe devices use INTERFACE_PCIE_DEVICE and for hybrid devices
         * use both.
         */
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};


/* Define our device type, this is done during startup of QEMU" */
static void pci_example_register_types(void)
{
    type_register_static(&pci_example_info);
}

type_init(pci_example_register_types)



