/*
 * QEMU x86 ISA testdev
 *
 * Copyright (c) 2012 Avi Kivity, Gerd Hoffmann, Marcelo Tosatti
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * This device is used to test KVM features specific to the x86 port, such
 * as emulation, power management, interrupt routing, among others. It's meant
 * to be used like:
 *
 * qemu-system-x86_64 -device pc-testdev -serial stdio \
 * -device isa-debug-exit,iobase=0xf4,iosize=0x4 \
 * -kernel /home/lmr/Code/virt-test.git/kvm/unittests/msr.flat
 *
 * Where msr.flat is one of the KVM unittests, present on a separate repo,
 * https://git.kernel.org/pub/scm/virt/kvm/kvm-unit-tests.git
*/

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "qom/object.h"
#include "sysemu/kvm.h"
#include <linux/kvm.h>
#include "hw/qdev-properties.h"

#define IOMEM_LEN    0x10000

struct PCTestdev {
    ISADevice parent_obj;

    MemoryRegion ioport;
    MemoryRegion ioport_byte;
    MemoryRegion flush;
    MemoryRegion irq;
    MemoryRegion iomem;
    uint32_t ioport_data;
    char iomem_buf[IOMEM_LEN];

    uint64_t guest_paddr;
    uint64_t memory_size;
    char *read_fifo;
    char *write_fifo;
    bool posted_writes;
    bool pio;
    int rfd;
    int wfd;
};

#define TYPE_TESTDEV "pc-testdev"
OBJECT_DECLARE_SIMPLE_TYPE(PCTestdev, TESTDEV)

static uint64_t test_irq_line_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void test_irq_line_write(void *opaque, hwaddr addr, uint64_t data,
                          unsigned len)
{
    PCTestdev *dev = opaque;
    ISADevice *isa = ISA_DEVICE(dev);

    qemu_set_irq(isa_get_irq(isa, addr), !!data);
}

static const MemoryRegionOps test_irq_ops = {
    .read = test_irq_line_read,
    .write = test_irq_line_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void test_ioport_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned len)
{
    PCTestdev *dev = opaque;
    int bits = len * 8;
    int start_bit = (addr & 3) * 8;
    uint32_t mask = ((uint32_t)-1 >> (32 - bits)) << start_bit;
    dev->ioport_data &= ~mask;
    dev->ioport_data |= data << start_bit;
}

static uint64_t test_ioport_read(void *opaque, hwaddr addr, unsigned len)
{
    PCTestdev *dev = opaque;
    int bits = len * 8;
    int start_bit = (addr & 3) * 8;
    uint32_t mask = ((uint32_t)-1 >> (32 - bits)) << start_bit;
    return (dev->ioport_data & mask) >> start_bit;
}

static const MemoryRegionOps test_ioport_ops = {
    .read = test_ioport_read,
    .write = test_ioport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps test_ioport_byte_ops = {
    .read = test_ioport_read,
    .write = test_ioport_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t test_flush_page_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void test_flush_page_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned len)
{
    hwaddr page = 4096;
    void *a = cpu_physical_memory_map(data & ~0xffful, &page, false);

    /* We might not be able to get the full page, only mprotect what we actually
       have mapped */
#if defined(CONFIG_POSIX)
    mprotect(a, page, PROT_NONE);
    mprotect(a, page, PROT_READ|PROT_WRITE);
#endif
    cpu_physical_memory_unmap(a, page, 0, 0);
}

static const MemoryRegionOps test_flush_ops = {
    .read = test_flush_page_read,
    .write = test_flush_page_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t test_iomem_read(void *opaque, hwaddr addr, unsigned len)
{
    PCTestdev *dev = opaque;
    uint64_t ret = 0;
    memcpy(&ret, &dev->iomem_buf[addr], len);

    return ret;
}

static void test_iomem_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned len)
{
    PCTestdev *dev = opaque;
    memcpy(&dev->iomem_buf[addr], &val, len);
    dev->iomem_buf[addr] = val;
}

static const MemoryRegionOps test_iomem_ops = {
    .read = test_iomem_read,
    .write = test_iomem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void testdev_realizefn(DeviceState *d, Error **errp)
{
    struct kvm_ioregion ioreg;
    int flags = 0;

    ISADevice *isa = ISA_DEVICE(d);
    PCTestdev *dev = TESTDEV(d);
    MemoryRegion *mem = isa_address_space(isa);
    MemoryRegion *io = isa_address_space_io(isa);

    memory_region_init_io(&dev->ioport, OBJECT(dev), &test_ioport_ops, dev,
                          "pc-testdev-ioport", 4);
    memory_region_init_io(&dev->ioport_byte, OBJECT(dev),
                          &test_ioport_byte_ops, dev,
                          "pc-testdev-ioport-byte", 4);
    memory_region_init_io(&dev->flush, OBJECT(dev), &test_flush_ops, dev,
                          "pc-testdev-flush-page", 4);
    memory_region_init_io(&dev->irq, OBJECT(dev), &test_irq_ops, dev,
                          "pc-testdev-irq-line", 24);
    memory_region_init_io(&dev->iomem, OBJECT(dev), &test_iomem_ops, dev,
                          "pc-testdev-iomem", IOMEM_LEN);

    memory_region_add_subregion(io,  0xe0,       &dev->ioport);
    memory_region_add_subregion(io,  0xe4,       &dev->flush);
    memory_region_add_subregion(io,  0xe8,       &dev->ioport_byte);
    memory_region_add_subregion(io,  0x2000,     &dev->irq);
    memory_region_add_subregion(mem, 0xff000000, &dev->iomem);

    if (!dev->guest_paddr || !dev->write_fifo) {
        return;
    }

    dev->wfd = open(dev->write_fifo, O_WRONLY);
    if (dev->wfd < 0) {
        error_report("failed to open write fifo %s", dev->write_fifo);
        return;
    }

    if (dev->read_fifo) {
        dev->rfd = open(dev->read_fifo, O_RDONLY);
        if (dev->rfd < 0) {
            error_report("failed to open read fifo %s", dev->read_fifo);
            close(dev->wfd);
            return;
        }
    }

    flags |= dev->pio ? KVM_IOREGION_PIO : 0;
    flags |= dev->posted_writes ? KVM_IOREGION_POSTED_WRITES : 0;
    ioreg.guest_paddr = dev->guest_paddr;
    ioreg.memory_size = dev->memory_size;
    ioreg.write_fd = dev->wfd;
    ioreg.read_fd = dev->rfd;
    ioreg.flags = flags;
    kvm_vm_ioctl(kvm_state, KVM_SET_IOREGION, &ioreg);
}

static void testdev_unrealizefn(DeviceState *d)
{
    struct kvm_ioregion ioreg;
    PCTestdev *dev = TESTDEV(d);

    if (!dev->guest_paddr || !dev->write_fifo) {
        return;
    }

    ioreg.guest_paddr = dev->guest_paddr;
    ioreg.memory_size = dev->memory_size;
    ioreg.flags = KVM_IOREGION_DEASSIGN;
    kvm_vm_ioctl(kvm_state, KVM_SET_IOREGION, &ioreg);
    close(dev->wfd);
    if (dev->rfd > 0) {
        close(dev->rfd);
    }
}

static Property ioregionfd_properties[] = {
    DEFINE_PROP_UINT64("addr", PCTestdev, guest_paddr, 0),
    DEFINE_PROP_UINT64("size", PCTestdev, memory_size, 0),
    DEFINE_PROP_STRING("rfifo", PCTestdev, read_fifo),
    DEFINE_PROP_STRING("wfifo", PCTestdev, write_fifo),
    DEFINE_PROP_BOOL("pio", PCTestdev, pio, false),
    DEFINE_PROP_BOOL("pw", PCTestdev, posted_writes, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void testdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = testdev_realizefn;
    dc->unrealize = testdev_unrealizefn;
    device_class_set_props(dc, ioregionfd_properties);
}

static const TypeInfo testdev_info = {
    .name           = TYPE_TESTDEV,
    .parent         = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(PCTestdev),
    .class_init     = testdev_class_init,
};

static void testdev_register_types(void)
{
    type_register_static(&testdev_info);
}

type_init(testdev_register_types)
