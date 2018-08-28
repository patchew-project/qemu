#include "qemu/osdep.h"
#include "hw/pci/pci.h"

#define TYPE_PCI_EXAMPLE "pci-example"

#define PCI_EXAMPLE(obj)  \
    OBJECT_CHECK(PCIExampleDevState, (obj), TYPE_PCI_EXAMPLE)


#define ERROR -1
#define BLOCK_SIZE 64
#define EXAMPLE_MMIO_SIZE BLOCK_SIZE
#define EXAMPLE_PIO_SIZE BLOCK_SIZE
#define DMA_BUFF_SIZE 4096

/*---------------------------------------------------------------------------*/
/*                                 PCI Struct                                */
/*---------------------------------------------------------------------------*/

typedef struct PCIExampleDevState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    /* memory region */
    MemoryRegion portio;
    MemoryRegion mmio;
    MemoryRegion irqio;
    MemoryRegion dmaio;

    /* data registers */
    uint64_t memData, ioData, dmaPhisicalBase;

    qemu_irq irq;
    /* for the driver to determine if this device caused the interrupt */
    uint64_t threwIrq;

} PCIExampleDevState;


/*---------------------------------------------------------------------------*/
/*                         Read/Write functions                              */
/*---------------------------------------------------------------------------*/

/* do nothing because the mmio read is done from DMA buffer */
static uint64_t pci_example_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return ERROR;
}

static void
pci_example_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;
    PCIDevice *pciDev = (PCIDevice *)opaque;

    if (size != 1) {
        return;
    }

    /* compute the result */
    pms->memData = val * 2;

    /* write the result directly to phisical memory */
    cpu_physical_memory_write(pms->dmaPhisicalBase, &pms->memData,
            DMA_BUFF_SIZE);

    /* raise an IRQ to notify DMA has finished  */
    pms->threwIrq = 1;
    pci_irq_assert(pciDev);
}

static uint64_t pci_example_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;

    if (size != 1) {
        return ERROR;
    }

    return pms->ioData;
}

static void
pci_example_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;
    PCIDevice *pciDev = (PCIDevice *)opaque;

    if (size != 1) {
        return;
    }

    pms->ioData = val * 2;
    pms->threwIrq = 1;
    pci_irq_assert(pciDev);
}

static uint64_t pci_example_irqio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;

    if (size != 1) {
        return ERROR;
    }

    return pms->threwIrq;
}

static void
pci_example_irqio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;
    PCIDevice *pciDev = (PCIDevice *)opaque;

    if (size != 1) {
        return;
    }

    /* give the ability to assert IRQ , we will use it only to deassert IRQ */
    if (val) {
        pci_irq_assert(pciDev);
        pms->threwIrq = 1;
    } else {
        pms->threwIrq = 0;
        pci_irq_deassert(pciDev);
    }
}

/* do nothing because physical DMA buffer addres is onlyt set and don't need to
 * be red */
static uint64_t
pci_example_dma_base_read(void *opaque, hwaddr addr, unsigned size)
{
    return ERROR;
}

static void
pci_example_dma_base_write(void *opaque, hwaddr addr, uint64_t val,
        unsigned size)
{
    PCIExampleDevState *pms = (PCIExampleDevState *)opaque;

    if (size != 4) {
        return;
    }

    /* notify the device about the physical address of the DMA buffer that the
     * driver has allocated */
    switch (addr) {
        /* lower bytes */
        case(0):
            pms->dmaPhisicalBase &= 0xffffffff00000000;
            break;

        /* upper bytes */
        case(4):
            val <<= 32;
            pms->dmaPhisicalBase &= 0x00000000ffffffff;
            break;
    }

    pms->dmaPhisicalBase |= val;
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

/* this is called when lunching the vm with "-device <device name>" */
static void pci_example_realize(PCIDevice *pciDev, Error **errp)
{
   PCIExampleDevState *d = PCI_EXAMPLE(pciDev);
   uint8_t *pciCond = pciDev->config;

   d->threwIrq = 0;

   /* initiallise the memory region of the CPU to the device */
   memory_region_init_io(&d->mmio, OBJECT(d), &pci_example_mmio_ops, d,
           "pci-example-mmio", EXAMPLE_MMIO_SIZE);

   memory_region_init_io(&d->portio, OBJECT(d), &pci_example_pio_ops, d,
           "pci-example-portio", EXAMPLE_PIO_SIZE);

   memory_region_init_io(&d->irqio, OBJECT(d), &pci_example_irqio_ops, d,
           "pci-example-irqio", EXAMPLE_PIO_SIZE);

   memory_region_init_io(&d->dmaio, OBJECT(d), &pci_example_dma_ops, d,
           "pci-example-dma-base", EXAMPLE_MMIO_SIZE);

   /* alocate BARs */
   pci_register_bar(pciDev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
   pci_register_bar(pciDev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->portio);
   pci_register_bar(pciDev, 2, PCI_BASE_ADDRESS_SPACE_IO, &d->irqio);
   pci_register_bar(pciDev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->dmaio);

   /* give interrupt support.
    * a pci device has 4 pin for interrupt, here we use pin A */
   pci_config_set_interrupt_pin(pciCond, 1);
}


/* the destructor of pci_example_realize() */
static void pci_example_uninit(PCIDevice *dev)
{
    /* unregister BARs and other stuff */
}


/* class constructor */
static void pci_example_class_init(ObjectClass *klass, void *data)
{
   /* sort of dynamic cast */
   DeviceClass *dc = DEVICE_CLASS(klass);
   PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

   k->realize = pci_example_realize;
   k->exit = pci_example_uninit;

   /* some regular IDs in HEXA */
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


/* Contains all the informations of the device we are creating.
 * class_init will be called when we are defining our device. */
static const TypeInfo pci_example_info = {
    .name           = TYPE_PCI_EXAMPLE,
    .parent         = TYPE_PCI_DEVICE,
    .instance_size  = sizeof(PCIExampleDevState),
    .class_init     = pci_example_class_init,
    .interfaces     = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};


/* function called before the qemu main it will define our device */
static void pci_example_register_types(void)
{
    type_register_static(&pci_example_info);
}

/* make qemu register our device at qemu-booting */
type_init(pci_example_register_types)



