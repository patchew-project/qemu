Motivation:
In heterogeneous computing system, accelorator generally exposes its device
memory to host via PCIe and CXL.mem(Compute Express Link) to share memory
between host and device, and these memory generally are uniformly managed by
host, they are called HDM (host managed device memory), further SVA (share
virtual address) can be achieved on this base. One computing device may be used
by multiple virtual machines if it supports SRIOV, to efficiently use device
memory in virtualization, each VM allocates device memory on-demand without
overcommit, but how to dynamically attach host memory resource to VM. A virtual
PCI device, dynamic_mdev, is introduced to achieve this target. dynamic_mdev
has a big bar space which size can be assigned by user when creating VM, the
bar doesn't have backend memory at initialization stage, later driver in guest
triggers QEMU to map host memory to the bar space. how much memory, when and
where memory will be mapped to are determined by guest driver, after device
memory has been attached to the virtual PCI bar, application in guest can
access device memory by the virtual PCI bar. Memory allocation and negotiation
are left to guest driver and memory backend implementation. dynamic_mdev is a
mechanism which provides significant benefits to heterogeneous memory
virtualization.

Implementation:
dynamic_mdev device has two bars, bar0 and bar2. bar0 is a 32-bit register bar
which used to host control register for control and message communication, Bar2
is a 64-bit mmio bar, which is used to attach host memory to, the bar size can
be assigned via parameter when creating VM. Host memory is attached to this bar
via mmap API.


          VM1                           VM2
 -----------------------        ----------------------
|      application      |      |     application      |
|                       |      |                      |
|-----------------------|      |----------------------|
|     guest driver      |      |     guest driver     |
|   |--------------|    |      |   | -------------|   |
|   | pci mem bar  |    |      |   | pci mem bar  |   |
 ---|--------------|-----       ---|--------------|---
     ----   ---                     --   ------
    |    | |   |                   |  | |      |
     ----   ---                     --   ------
            \                      /
             \                    /
              \                  /
               \                /
                |              |
                V              V
 --------------- /dev/mdev.mmap ------------------------
|     --   --   --   --   --   --                       |
|    |  | |  | |  | |  | |  | |  |  <-----free_mem_list |
|     --   --   --   --   --   --                       |
|                                                       |
|                       HDM(host managed device memory )|
 -------------------------------------------------------

1. Create device:
-device dyanmic-mdevice,size=0x200000000,align=0x40000000,mem-path=/dev/mdev

size: bar space size
aglin: alignment of dynamical attached memory
mem-path: host backend memory device


2. Registers to control dynamical memory attach
All register is placed in bar0

        INT_MASK     =     0, /* RW */
        INT_STATUS   =     4, /* RW: write 1 clear */
        DOOR_BELL    =     8, /*
                               * RW: trigger device to act
                               *  31        15        0
                               *  --------------------
                               * |en|xxxxxxxx|  cmd   |
                               *  --------------------
                               */

        /* RO: 4k, 2M, 1G aglign for memory size */
        MEM_ALIGN   =      12,

        /* RO: offset in memory bar shows bar space has had ram map */
        HW_OFFSET    =     16,

        /* RW: size of dynamical attached memory */
        MEM_SIZE     =     24,

        /* RW: offset in host mdev, which dynamical attached memory from  */
        MEM_OFFSET   =     32,

3. To trigger QEMU to attach a memory, guest driver makes following operation:

        /* memory size */
        writeq(size, reg_base + 0x18);

        /* backend file offset */
        writeq(offset, reg_base + 0x20);

        /* trigger device to map memory from host */
        writel(0x80000001, reg_base + 0x8);

        /* wait for reply from backend */
        wait_for_completion(&attach_cmp);

4. QEMU implementation
dynamic_mdev utilizes QEMU's memory model to dynamically add memory region to
memory container, the model is described at qemu/docs/devel/memory.rst
The below steps will describe the whole flow:
   1> create a virtual PCI device
   2> pci register bar with memory region container, which only define bar size
   3> guest driver requests memory via register interaction, and it tells QEMU
      about memory size, backend memory offset, and so on
   4> QEMU receives request from guest driver, then apply host memory from
      backend file via mmap(), QEMU use the allocated RAM to create a memory
      region through memory_region_init_ram(), and attach this memory region to
      bar container through calling memory_region_add_subregion_overlap(). After
      that KVM build gap->hpa mapping
   5> QEMU sends MSI to guest driver that dynamical memory attach completed
You could refer to source code for more detail.


Backend memory device
Backend device can be a stardard share memory file with standard mmap() support
It also may be a specific char device with mmap() implementation.
In a word, how to implement this device is user responsibility.
