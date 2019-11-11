IVSHMEM Device Specification
============================

** NOTE: THIS IS WORK-IN-PROGRESS, NOT YET A STABLE INTERFACE SPECIFICATION! **

The Inter-VM Shared Memory device provides the following features to its users:

- Interconnection between up to 65536 peers

- Multi-purpose shared memory region

    - common read/writable section

    - unidirectional sections that are read/writable for one peer and only
      readable for the others

    - section with peer states

- Event signaling via interrupt to remote sides

- Support for life-cycle management via state value exchange and interrupt
  notification on changes, backed by a shared memory section

- Free choice of protocol to be used on top

- Protocol type declaration

- Unprivileged access to memory-mapped or I/O registers feasible

- Discoverable and configurable via standard PCI mechanisms


Hypervisor Model
----------------

In order to provide a consistent link between peers, all connected instances of
IVSHMEM devices need to be configured, created and run by the hypervisor
according to the following requirements:

- The instances of the device need to be accessible via PCI programming
  interfaces on all sides.

- The read/write shared memory section has to be of the same size for all
  peers and, if non-zero, has to reflect the same memory content for them.

- If output sections are present (non-zero section size), there must be one
  reserved for each peer with exclusive write access. All output sections
  must have the same size and must be readable for all peers. They have to
  reflect the same memory content for all peers.

- The State Table must have the same size for all peers, must be large enough to
  hold a state values of all peers, and must be read-only for the user.

- State register changes (explicit writes, peer resets) have to be propagated
  to the other peers by updating the corresponding State Table entry and issuing
  an interrupt to all other peers if they enabled reception.

- Interrupts events triggered by a peer have to be delivered to the target peer,
  provided the receiving side is valid and has enabled the reception.

- All peers must have the same interrupt delivery features available, i.e. MSI-X
  with the same maximum number of vectors on platforms supporting this
  mechanism, otherwise INTx with one vector.


Guest-side Programming Model
----------------------------

An IVSHMEM device appears as a PCI device to its users. Unless otherwise noted,
it conforms to the PCI Local Bus Specification, Revision 3.0 As such, it is
discoverable via the PCI configuration space and provides a number of standard
and custom PCI configuration registers.

### Shared Memory Region Layout

The shared memory region is divided into several sections.

    +-----------------------------+   -
    |                             |   :
    | Output Section for peer n-1 |   : Output Section Size
    |     (n = Maximum Peers)     |   :
    +-----------------------------+   -
    :                             :
    :                             :
    :                             :
    +-----------------------------+   -
    |                             |   :
    |  Output Section for peer 1  |   : Output Section Size
    |                             |   :
    +-----------------------------+   -
    |                             |   :
    |  Output Section for peer 0  |   : Output Section Size
    |                             |   :
    +-----------------------------+   -
    |                             |   :
    |     Read/Write Section      |   : R/W Section Size
    |                             |   :
    +-----------------------------+   -
    |                             |   :
    |         State Table         |   : State Table Size
    |                             |   :
    +-----------------------------+   <-- Shared memory base address

The first section consists of the mandatory State Table. Its size is defined by
the State Table Size register and cannot be zero. This section is read-only for
all peers.

The second section consists of shared memory that is read/writable for all
peers. Its size is defined by the R/W Section Size register. A size of zero is
permitted.

The third and following sections are unidirectional output sections, one for
each peer. Their sizes are all identical. The size of a single output section is
defined by the Output Section Size register. An output section is read/writable
for the corresponding peer and read-only for all other peers. E.g., only the
peer with ID 3 can write to the fourths output section, but all peers can read
from this section.

All sizes have to be rounded up to multiples of a mappable page in order to
allow access control according to the section restrictions.

### Configuration Space Registers

#### Header Registers

| Offset | Register               | Content                                              |
|-------:|:-----------------------|:-----------------------------------------------------|
|    00h | Vendor ID              | 1AF4h                                                |
|    02h | Device ID              | 1110h                                                |
|    04h | Command Register       | 0000h on reset, implementing bits 1, 2, 10           |
|    06h | Status Register        | 0010h, static value (bit 3 not implemented)          |
|    08h | Revision ID            | 02h                                                  |
|    09h | Class Code, Interface  | Protocol Type bits 0-7, see [Protocols](#Protocols)  |
|    0Ah | Class Code, Sub-Class  | Protocol Type bits 8-15, see [Protocols](#Protocols) |
|    0Bh | Class Code, Base Class | FFh                                                  |
|    0Eh | Header Type            | 00h                                                  |
|    10h | BAR 0                  | MMIO or I/O register region                          |
|    14h | BAR 1                  | MSI-X region                                         |
|    18h | BAR 2 (with BAR 3)     | optional: 64-bit shared memory region                |
|    2Ch | Subsystem Vendor ID    | same as Vendor ID, or provider-specific value        |
|    2Eh | Subsystem ID           | same as Device ID, or provider-specific value        |
|    34h | Capability Pointer     | First capability                                     |
|    3Eh | Interrupt Pin          | 01h-04h, must be 00h if MSI-X is available           |

If BAR 2 is not present, the shared memory region is not relocatable by the
user. In that case, the hypervisor has to implement the Base Address register in
the vendor-specific capability.

Other header registers may not be implemented. If not implemented, they return 0
on read and ignore write accesses.

#### Vendor Specific Capability (ID 09h)

| Offset | Register            | Content                                        |
|-------:|:--------------------|:-----------------------------------------------|
|    00h | ID                  | 09h                                            |
|    01h | Next Capability     | Pointer to next capability or 00h              |
|    02h | Length              | 18h or 20h                                     |
|    03h | Privileged Control  | Bit 0 (read/write): one-shot interrupt mode    |
|        |                     | Bits 1-7: RsvdZ                                |
|    04h | State Table Size    | 32-bit size of read-only State Table           |
|    08h | R/W Section Size    | 64-bit size of common read/write section       |
|    10h | Output Section Size | 64-bit size of unidirectional output sections  |
|    18h | Base Address        | optional: 64-bit base address of shared memory |

All registers are read-only, except for bit 0 of the Privileged Control
register.

When bit 0 in the Privileged Control register is set to 1, the device clears
bit 0 in the Interrupt Control register on each interrupt delivery. This enables
automatic interrupt throttling when re-enabling shall be performed by a
scheduled unprivileged instance on the user side.

If an IVSHMEM device does not support a relocatable shared memory region, BAR 2
must not be implemented by the provider. Instead, the Base Address register has
to be implemented to report the location of the shared memory region in the
user's address space.

A non-existing shared memory section has to report zero in its Section Size
register.

#### MSI-X Capability (ID 11h)

On platforms supporting MSI-X, IVSHMEM has to provide interrupt delivery via
this mechanism. In that case, the legacy INTx delivery mechanism is not
available, and the Interrupt Pin configuration register returns 0.

The IVSHMEM device has no notion of pending interrupts. Therefore, reading from
the MSI-X Pending Bit Array will always return 0.

The corresponding MSI-X MMIO region is configured via BAR 1.

The MSI-X table size reported by the MSI-X capability structure is identical for
all peers.

### Register Region

The register region may be implemented as MMIO or I/O.

When implementing it as MMIO, the hypervisor has to ensure that the register
region can be mapped as a single page into the address space of the user. Write
accesses to MMIO region offsets that are not backed by registers have to be
ignored, read accesses have to return 0. This enables the user to hand out the
complete region, along with the shared memory, to an unprivileged instance.

The region location in the user's physical address space is configured via BAR
0. The following table visualizes the region layout:

| Offset | Register                                                            |
|-------:|:--------------------------------------------------------------------|
|    00h | ID                                                                  |
|    04h | Maximum Peers                                                       |
|    08h | Features                                                            |
|    0Ch | Interrupt Control                                                   |
|    10h | Doorbell                                                            |
|    14h | State                                                               |

All registers support only aligned 32-bit accesses.

#### ID Register (Offset 00h)

Read-only register that reports the ID of the local device. It is unique for all
of the connected devices and remains unchanged over their lifetime.

#### Maximum Peers Register (Offset 04h)

Read-only register that reports the maximum number of possible peers (including
the local one). The supported range is between 2 and 65536 and remains constant
over the lifetime of all peers.

#### Features Register (Offset 08h)

Read-only register that reports features of the local device or the connected
peers. Its content remains constant over the lifetime of all peers.

| Bits | Content                                                               |
|-----:|:----------------------------------------------------------------------|
|    0 | 1: Synchronized shared memory base address                            |
| 1-31 | RsvdZ                                                                 |

If "synchronized shared memory base address" is reported (bit 0 is set), the
shared memory region is mapped at the same address into the user address spaces
of all connected peers. Thus, peers can use physical addresses as pointers when
exchanging information via the shared memory. This feature flag is never set
when the shared memory region is relocatable via BAR 2.

#### Interrupt Control Register (Offset 0Ch)

This read/write register controls the generation of interrupts whenever a peer
writes to the Doorbell register or changes its state.

| Bits | Content                                                               |
|-----:|:----------------------------------------------------------------------|
|    0 | 1: Enable interrupt generation                                        |
| 1-31 | RsvdZ                                                                 |

Note that bit 0 is reset to 0 on interrupt delivery if one-shot interrupt mode
is enabled in the Enhanced Features register.

The value of this register after device reset is 0.

#### Doorbell Register (Offset 10h)

Write-only register that triggers an interrupt vector in the target device if it
is enabled there.

| Bits  | Content                                                              |
|------:|:---------------------------------------------------------------------|
|  0-15 | Vector number                                                        |
| 16-31 | Target ID                                                            |

Writing a vector number that is not enabled by the target has no effect. The
peers can derive the number of available vectors from their own device
capabilities and are expected to define or negotiate the used ones via the
selected protocol.

Addressing a non-existing or inactive target has no effect. Peers can identify
active targets via the State Table.

The behavior on reading from this register is undefined.

#### State Register (Offset 14h)

Read/write register that defines the state of the local device. Writing to this
register sets the state and triggers interrupt vector 0 on the remote device if
the written state value differs from the previous one. The user of the remote
device can read the value written to this register from the State Table.

The value of this register after device reset is 0.

### State Table

The State Table is a read-only section at the beginning of the shared memory
region. It contains a 32-bit state value for each of the peers. Locating the
table in shared memory allows fast checking of remote states without register
accesses.

The table is updated on each state change of a peers. Whenever a user of an
IVSHMEM device writes a value to the Local State register, this value is copied
into the corresponding entry of the State Table. When a IVSHMEM device is reset
or disconnected from the other peers, zero is written into the corresponding
table entry. The initial content of the table is all zeros.

    +--------------------------------+
    | 32-bit state value of peer n-1 |
    +--------------------------------+
    :                                :
    +--------------------------------+
    | 32-bit state value of peer 1   |
    +--------------------------------+
    | 32-bit state value of peer 0   |
    +--------------------------------+ <-- Shared memory base address


Protocols
---------

The IVSHMEM device shall support the peers of a connection in agreeing on the
protocol used over the shared memory devices. For that purpose, the interface
byte (offset 09h) and the sub-class byte (offset 0Ah) of the Class Code register
encodes a 16-bit protocol type for the users. The following type values are
defined:

| Protocol Type | Description                                                  |
|--------------:|:-------------------------------------------------------------|
|         0000h | Undefined type                                               |
|         0001h | Virtual peer-to-peer Ethernet                                |
|   0002h-3FFFh | Reserved                                                     |
|   4000h-7FFFh | User-defined protocols                                       |
|   8000h-BFFFh | Virtio over Shared Memory, front-end peer                    |
|   C000h-FFFFh | Virtio over Shared Memory, back-end peer                     |

Details of the protocols are not in the scope of this specification.
