***************************************
VFIO-over-socket Protocol Specification
***************************************

Version 0.1

Introduction
============
VFIO-over-socket, also known as vfio-user, is a protocol that allows a device
to be virtualized in a separate process outside of QEMU. VFIO-over-socket
devices consist of a generic VFIO device type, living inside QEMU, which we
call the client, and the core device implementation, living outside QEMU, which
we call the server. VFIO-over-socket can be the main transport mechanism for
multi-process QEMU, however it can be used by other applications offering
device virtualization. Explaining the advantages of a
disaggregated/multi-process QEMU, and device virtualization outside QEMU in
general, is beyond the scope of this document.

This document focuses on specifying the VFIO-over-socket protocol. VFIO has
been chosen for the following reasons:

1) It is a mature and stable API, backed by an extensively used framework.
2) The existing VFIO client implementation (qemu/hw/vfio/) can be largely
   reused.

In a proof of concept implementation it has been demonstrated that using VFIO
over a UNIX domain socket is a viable option. VFIO-over-socket is designed with
QEMU in mind, however it could be used by other client applications. The
VFIO-over-socket protocol does not require that QEMU's VFIO client
implementation is used in QEMU. None of the VFIO kernel modules are required
for supporting the protocol, neither in the client nor the server, only the
source header files are used.

The main idea is to allow a virtual device to function in a separate process in
the same host over a UNIX domain socket. A UNIX domain socket (AF_UNIX) is
chosen because we can trivially send file descriptors over it, which in turn
allows:

* Sharing of guest memory for DMA with the virtual device process.
* Sharing of virtual device memory with the guest for fast MMIO.
* Efficient sharing of eventfd's for triggering interrupts.

However, other socket types could be used which allows the virtual device
process to run in a separate guest in the same host (AF_VSOCK) or remotely
(AF_INET). Theoretically the underlying transport doesn't necessarily have to
be a socket, however we don't examine such alternatives. In this document we
focus on using a UNIX domain socket and introduce basic support for the other
two types of sockets without considering performance implications.

This document does not yet describe any internal details of the server-side
implementation, however QEMU's VFIO client implementation will have to be
adapted according to this protocol in order to support VFIO-over-socket virtual
devices.

VFIO
====
VFIO is a framework that allows a physical device to be securely passed through
to a user space process; the kernel does not drive the device at all.
Typically, the user space process is a VM and the device is passed through to
it in order to achieve high performance. VFIO provides an API and the required
functionality in the kernel. QEMU has adopted VFIO to allow a guest virtual
machine to directly access physical devices, instead of emulating them in
software

VFIO-over-socket reuses the core VFIO concepts defined in its API, but
implements them as messages to be sent over a UNIX-domain socket. It does not
change the kernel-based VFIO in any way, in fact none of the VFIO kernel
modules need to be loaded to use VFIO-over-socket. It is also possible for QEMU
to concurrently use the current kernel-based VFIO for one guest device, and use
VFIO-over-socket for another device in the same guest.

VFIO Device Model
-----------------
A device under VFIO presents a standard VFIO model to the user process. Many
of the VFIO operations in the existing kernel model use the ioctl() system
call, and references to the existing model are called the ioctl()
implementation in this document.

The following sections describe the set of messages that implement the VFIO
device model over a UNIX domain socket. In many cases, the messages are direct
translations of data structures used in the ioctl() implementation. Messages
derived from ioctl()s will have a name derived from the ioctl() command name.
E.g., the VFIO_GET_INFO ioctl() command becomes a VFIO_USER_GET_INFO message.
The purpose for this reuse is to share as much code as feasible with the
ioctl() implementation.

Client and Server
^^^^^^^^^^^^^^^^^
The socket connects two processes together: a client process and a server
process. In the context of this document, the client process is the process
emulating a guest virtual machine, such as QEMU. The server process is a
process that provides device emulation.

Connection Initiation
^^^^^^^^^^^^^^^^^^^^^
After the client connects to the server, the initial server message is
VFIO_USER_VERSION to propose a protocol version and set of capabilities to
apply to the session. The client replies with a compatible version and set of
capabilities it will support, or closes the connection if it cannot support the
advertised version.

Guest Memory Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^
The client uses VFIO_USER_DMA_MAP and VFIO_USER_DMA_UNMAP messages to inform
the server of the valid guest DMA ranges that the server can access on behalf
of a device. Guest memory may be accessed by the server via VFIO_USER_DMA_READ
and VFIO_USER_DMA_WRITE messages over the socket.

An optimization for server access to guest memory is for the client to provide
file descriptors the server can mmap() to directly access guest memory. Note
that mmap() privileges cannot be revoked by the client, therefore file
descriptors should only be exported in environments where the client trusts the
server not to corrupt guest memory.

Device Information
^^^^^^^^^^^^^^^^^^
The client uses a VFIO_USER_DEVICE_GET_INFO message to query the server for
information about the device. This information includes:

* The device type and capabilities,
* the number of memory regions, and
* the device presents to the guest the number of interrupt types the device
  supports.

Region Information
^^^^^^^^^^^^^^^^^^
The client uses VFIO_USER_DEVICE_GET_REGION_INFO messages to query the server
for information about the device's memory regions. This information describes:

* Read and write permissions, whether it can be memory mapped, and whether it
  supports additional capabilities.
* Region index, size, and offset.

When a region can be mapped by the client, the server provides a file
descriptor which the client can mmap(). The server is responsible for polling
for client updates to memory mapped regions.

Region Capabilities
"""""""""""""""""""
Some regions have additional capabilities that cannot be described adequately
by the region info data structure. These capabilities are returned in the
region info reply in a list similar to PCI capabilities in a PCI device's
configuration space.

Sparse Regions
""""""""""""""
A region can be memory-mappable in whole or in part. When only a subset of a
region can be mapped by the client, a VFIO_REGION_INFO_CAP_SPARSE_MMAP
capability is included in the region info reply. This capability describes
which portions can be mapped by the client.

For example, in a virtual NVMe controller, sparse regions can be used so that
accesses to the NVMe registers (found in the beginning of BAR0) are trapped (an
infrequent an event), while allowing direct access to the doorbells (an
extremely frequent event as every I/O submission requires a write to BAR0),
found right after the NVMe registers in BAR0.

Interrupts
^^^^^^^^^^
The client uses VFIO_USER_DEVICE_GET_IRQ_INFO messages to query the server for
the device's interrupt types. The interrupt types are specific to the bus the
device is attached to, and the client is expected to know the capabilities of
each interrupt type. The server can signal an interrupt either with
VFIO_USER_VM_INTERRUPT messages over the socket, or can directly inject
interrupts into the guest via an event file descriptor. The client configures
how the server signals an interrupt with VFIO_USER_SET_IRQS messages.

Device Read and Write
^^^^^^^^^^^^^^^^^^^^^
When the guest executes load or store operations to device memory, the client
forwards these operations to the server with VFIO_USER_REGION_READ or
VFIO_USER_REGION_WRITE messages. The server will reply with data from the
device on read operations or an acknowledgement on write operations.

DMA
^^^
When a device performs DMA accesses to guest memory, the server will forward
them to the client with VFIO_USER_DMA_READ and VFIO_USER_DMA_WRITE messages.
These messages can only be used to access guest memory the client has
configured into the server.

Protocol Specification
======================
To distinguish from the base VFIO symbols, all VFIO-over-socket symbols are
prefixed with vfio_user or VFIO_USER. In revision 0.1, all data is in the
little-endian format, although this may be relaxed in future revision in cases
where the client and server are both big-endian. The messages are formatted
for seamless reuse of the native VFIO structs. A server can serve:

1) multiple clients, and/or
2) multiple virtual devices, belonging to one or more clients.

Therefore each message requires a header that uniquely identifies the virtual
device. It is a server-side implementation detail whether a single server
handles multiple virtual devices from the same or multiple guests.

Socket
------
A single UNIX domain socket is assumed to be used for each device. The location
of the socket is implementation-specific. Multiplexing clients, devices, and
servers over the same socket is not supported in this version of the protocol,
but a device ID field exists in the message header so that a future support can
be added without a major version change.

Authentication
--------------
For AF_UNIX, we rely on OS mandatory access controls on the socket files,
therefore it is up to the management layer to set up the socket as required.
Socket types than span guests or hosts will require a proper authentication
mechanism. Defining that mechanism is deferred to a future version of the
protocol.

Request Concurrency
-------------------
There can be multiple outstanding requests per virtual device, e.g. a
frame buffer where the guest does multiple stores to the virtual device. The
server can execute and reorder non-conflicting requests in parallel, depending
on the device semantics.

Socket Disconnection Behavior
-----------------------------
The server and the client can disconnect from each other, either intentionally
or unexpectedly. Both the client and the server need to know how to handle such
events.

Server Disconnection
^^^^^^^^^^^^^^^^^^^^
A server disconnecting from the client may indicate that:

1) A virtual device has been restarted, either intentionally (e.g. because of a
device update) or unintentionally (e.g. because of a crash). In any case, the
virtual device will come back so the client should not do anything (e.g. simply
reconnect and retry failed operations).

2) A virtual device has been shut down with no intention to be restarted.

It is impossible for the client to know whether or not a failure is
intermittent or innocuous and should be retried, therefore the client should
attempt to reconnect to the socket. Since an intentional server restart (e.g.
due to an upgrade) might take some time, a reasonable timeout should be used.
In cases where the disconnection is expected (e.g. the guest shutting down), no
new requests will be sent anyway so this situation doesn't pose a problem. The
control stack will clean up accordingly.

Parametrizing this behaviour by having the virtual device advertise a
reasonable reconnect is deferred to a future version of the protocol.

Client Disconnection
^^^^^^^^^^^^^^^^^^^^
The client disconnecting from the server primarily means that the QEMU process
has exited. Currently this means that the guest is shut down so the device is
no longer needed therefore the server can automatically exit. However, there
can be cases where a client disconnect should not result in a server exit:

1) A single server serving multiple clients.
2) A multi-process QEMU upgrading itself step by step, which isn't yet
   implemented.

Therefore in order for the protocol to be forward compatible the server should
take no action when the client disconnects. If anything happens to the client
process the control stack will know about it and can clean up resources
accordingly.

Request Retry and Response Timeout
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
QEMU's VFIO retries certain operations if they fail. While this makes sense for
real HW, we don't know for sure whether it makes sense for virtual devices. A
failed request is a request that has been successfully sent and has been
responded to with an error code. Failure to send the request in the first place
(e.g. because the socket is disconnected) is a different type of error examined
earlier in the disconnect section.

Defining a retry and timeout scheme if deferred to a future version of the
protocol.

Commands
--------
The following table lists the VFIO message command IDs, and whether the
message request is sent from the client or the server.

+----------------------------------+---------+-------------------+
| Name                             | Command | Request Direction |
+==================================+=========+===================+
| VFIO_USER_VERSION                | 1       | server → client   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DMA_MAP                | 2       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DMA_UNMAP              | 3       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DEVICE_GET_INFO        | 4       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DEVICE_GET_REGION_INFO | 5       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DEVICE_GET_IRQ_INFO    | 6       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DEVICE_SET_IRQS        | 7       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_REGION_READ            | 8       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_REGION_WRITE           | 9       | client → server   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DMA_READ               | 10      | server → client   |
+----------------------------------+---------+-------------------+
| VFIO_USER_DMA_READ               | 11      | server → client   |
+----------------------------------+---------+-------------------+
| VFIO_USER_VM_INTERRUPT           | 12      | server → client   |
+----------------------------------+---------+-------------------+
| VFIO_DEVICE_RESET                | 13      | client → server   |
+----------------------------------+---------+-------------------+

Header
------
All messages are preceded by a 16 byte header that contains basic information
about the message. The header is followed by message-specific data described
in the sections below.

+----------------+--------+-------------+
| Name           | Offset | Size        |
+================+========+=============+
| Device ID      | 0      | 2           |
+----------------+--------+-------------+
| Message ID     | 2      | 2           |
+----------------+--------+-------------+
| Command        | 4      | 4           |
+----------------+--------+-------------+
| Message size   | 8      | 4           |
+----------------+--------+-------------+
| Flags          | 12     | 4           |
+----------------+--------+-------------+
|                | +-----+------------+ |
|                | | Bit | Definition | |
|                | +=====+============+ |
|                | | 0   | Reply      | |
|                | +-----+------------+ |
|                | | 1   | No_reply   | |
|                | +-----+------------+ |
+----------------+--------+-------------+
| <message data> | 16     | variable    |
+----------------+--------+-------------+

* Device ID identifies the destination device of the message. This field is
  reserved when the server only supports one device per socket.
* Message ID identifies the message, and is used in the message acknowledgement.
* Command specifies the command to be executed, listed in the Command Table.
* Message size contains the size of the entire message, including the header.
* Flags contains attributes of the message:

  * The reply bit differentiates request messages from reply messages. A reply
    message acknowledges a previous request with the same message ID.
  * No_reply indicates that no reply is needed for this request. This is
    commonly used when multiple requests are sent, and only the last needs
    acknowledgement.

VFIO_USER_VERSION
-----------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | 0                      |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 1                      |
+--------------+------------------------+
| Message size | 16 + version length    |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| Version      | JSON byte array        |
+--------------+------------------------+

This is the initial message sent by the server after the socket connection is
established. The version is in JSON format, and the following objects must be
included:

+--------------+--------+---------------------------------------------------+
| Name         | Type   | Description                                       |
+==============+========+===================================================+
| version      | object | {“major”: <number>, “minor”: <number>}            |
|              |        | Version supported by the sender, e.g. “0.1”.      |
+--------------+--------+---------------------------------------------------+
| type         | string | Fixed to “vfio-user”.                             |
+--------------+--------+---------------------------------------------------+
| capabilities | array  | Reserved. Can be omitted for v0.1, otherwise must |
|              |        | be empty.                                         |
+--------------+--------+---------------------------------------------------+

Versioning and Feature Support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Upon accepting a connection, the server must send a VFIO_USER_VERSION message
proposing a protocol version and a set of capabilities. The client compares
these with the versions and capabilities it supports and sends a
VFIO_USER_VERSION reply according to the following rules.

* The major version in the reply must be the same as proposed. If the client
  does not support the proposed major, it closes the connection.
* The minor version in the reply must be equal to or less than the minor
  version proposed.
* The capability list must be a subset of those proposed. If the client
  requires a capability the server did not include, it closes the connection.
* If type is not “vfio-user”, the client closes the connection.

The protocol major version will only change when incompatible protocol changes
are made, such as changing the message format. The minor version may change
when compatible changes are made, such as adding new messages or capabilities,
Both the client and server must support all minor versions less than the
maximum minor version it supports. E.g., an implementation that supports
version 1.3 must also support 1.0 through 1.2.

VFIO_USER_DMA_MAP
-----------------

VFIO_USER_DMA_UNMAP
-------------------

Message Format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | 0                      |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | MAP=2, UNMAP=3         |
+--------------+------------------------+
| Message size | 16 + table size        |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| Table        | array of table entries |
+--------------+------------------------+

This message is sent by the client to the server to inform it of the guest
memory regions the device can access. It must be sent before the device can
perform any DMA to the guest. It is normally sent directly after the version
handshake is completed, but may also occur when memory is added or subtracted
in the guest.

The table is an array of the following structure. This structure is 32 bytes
in size, so the message size will be 16 + (# of table entries * 32). If a
region being added can be directly mapped by the server, an array of file
descriptors will be sent as part of the message meta-data. Each region entry
will have a corresponding file descriptor. On AF_UNIX sockets, the file
descriptors will be passed as SCM_RIGHTS type ancillary data.

Table entry format
^^^^^^^^^^^^^^^^^^

+-------------+--------+-------------+
| Name        | Offset | Size        |
+=============+========+=============+
| Address     | 0      | 8           |
+-------------+--------+-------------+
| Size        | 8      | 8           |
+-------------+--------+-------------+
| Offset      | 16     | 8           |
+-------------+--------+-------------+
| Protections | 24     | 4           |
+-------------+--------+-------------+
| Flags       | 28     | 4           |
+-------------+--------+-------------+
|             | +-----+------------+ |
|             | | Bit | Definition | |
|             | +=====+============+ |
|             | | 0   | Mappable   | |
|             | +-----+------------+ |
+-------------+--------+-------------+

* Address is the base DMA address of the region.
* Size is the size of the region.
* Offset is the file offset of the region with respect to the associated file
  descriptor.
* Protections are the region's protection attributes as encoded in
  ``<sys/mman.h>``.
* Flags contain the following region attributes:

  * Mappable indicate the region can be mapped via the mmap() system call using
    the file descriptor provided in the message meta-data.

VFIO_USER_DEVICE_GET_INFO
-------------------------

Message format
^^^^^^^^^^^^^^

+--------------+----------------------------+
| Name         | Value                      |
+==============+============================+
| Device ID    | <ID>                       |
+--------------+----------------------------+
| Message ID   | <ID>                       |
+--------------+----------------------------+
| Command      | 4                          |
+--------------+----------------------------+
| Message size | 16 in request, 32 in reply |
+--------------+----------------------------+
| Flags        | Reply bit set in reply     |
+--------------+----------------------------+
| Device info  | VFIO device info           |
+--------------+----------------------------+

This message is sent by the client to the server to query for basic information
about the device. Only the message header is needed in the request message.
The VFIO device info structure is defined in ``<sys/vfio.h>`` (``struct
vfio_device_info``).

VFIO device info format
^^^^^^^^^^^^^^^^^^^^^^^

+-------------+--------+--------------------------+
| Name        | Offset | Size                     |
+=============+========+==========================+
| argsz       | 16     | 4                        |
+-------------+--------+--------------------------+
| flags       | 20     | 4                        |
+-------------+--------+--------------------------+
|             | +-----+-------------------------+ |
|             | | Bit | Definition              | |
|             | +=====+=========================+ |
|             | | 0   | VFIO_DEVICE_FLAGS_RESET | |
|             | +-----+-------------------------+ |
|             | | 1   | VFIO_DEVICE_FLAGS_PCI   | |
|             | +-----+-------------------------+ |
+-------------+--------+--------------------------+
| num_regions | 24     | 4                        |
+-------------+--------+--------------------------+
| num_irqs    | 28     | 4                        |
+-------------+--------+--------------------------+

* argz is reserved in vfio-user, it is only used in the ioctl() VFIO
  implementation.
* flags contains the following device attributes.

  * VFIO_DEVICE_FLAGS_RESET indicates the device supports the
    VFIO_USER_DEVICE_RESET message.
  * VFIO_DEVICE_FLAGS_PCI indicates the device is a PCI device.

* num_regions is the number of memory regions the device exposes.
* num_irqs is the number of distinct interrupt types the device supports.

This version of the protocol only supports PCI devices. Additional devices may
be supported in future versions.

VFIO_USER_DEVICE_GET_REGION_INFO
--------------------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------+
| Name         | Value            |
+==============+==================+
| Device ID    | <ID>             |
+--------------+------------------+
| Message ID   | <ID>             |
+--------------+------------------+
| Command      | 5                |
+--------------+------------------+
| Message size | 48 + any caps    |
+--------------+------------------+
| Flags Reply  | bit set in reply |
+--------------+------------------+
| Region info  | VFIO region info |
+--------------+------------------+

This message is sent by the client to the server to query for information about
device memory regions. The VFIO region info structure is defined in
``<sys/vfio.h>`` (``struct vfio_region_info``).

VFIO region info format
^^^^^^^^^^^^^^^^^^^^^^^

+------------+--------+------------------------------+
| Name       | Offset | Size                         |
+============+========+==============================+
| argsz      | 16     | 4                            |
+------------+--------+------------------------------+
| flags      | 20     | 4                            |
+------------+--------+------------------------------+
|            | +-----+-----------------------------+ |
|            | | Bit | Definition                  | |
|            | +=====+=============================+ |
|            | | 0   | VFIO_REGION_INFO_FLAG_READ  | |
|            | +-----+-----------------------------+ |
|            | | 1   | VFIO_REGION_INFO_FLAG_WRITE | |
|            | +-----+-----------------------------+ |
|            | | 2   | VFIO_REGION_INFO_FLAG_MMAP  | |
|            | +-----+-----------------------------+ |
|            | | 3   | VFIO_REGION_INFO_FLAG_CAPS  | |
|            | +-----+-----------------------------+ |
+------------+--------+------------------------------+
| index      | 24     | 4                            |
+------------+--------+------------------------------+
| cap_offset | 28     | 4                            |
+------------+--------+------------------------------+
| size       | 32     | 8                            |
+------------+--------+------------------------------+
| offset     | 40     | 8                            |
+------------+--------+------------------------------+

* argz is reserved in vfio-user, it is only used in the ioctl() VFIO
  implementation.
* flags are attributes of the region:

  * VFIO_REGION_INFO_FLAG_READ allows client read access to the region.
  * VFIO_REGION_INFO_FLAG_WRITE allows client write access region.
  * VFIO_REGION_INFO_FLAG_MMAP specifies the client can mmap() the region. When
    this flag is set, the reply will include a file descriptor in its meta-data.
    On AF_UNIX sockets, the file descriptors will be passed as SCM_RIGHTS type
    ancillary data.
  * VFIO_REGION_INFO_FLAG_CAPS indicates additional capabilities found in the
    reply.

* index is the index of memory region being queried, it is the only field that
  is required to be set in the request message.
* cap_offset describes where additional region capabilities can be found.
  cap_offset is relative to the beginning of the VFIO region info structure.
  The data structure it points is a VFIO cap header defined in ``<sys/vfio.h>``.
* size is the size of the region.
* offset is the offset given to the mmap() system call for regions with the
  MMAP attribute. It is also used as the base offset when mapping a VFIO
  sparse mmap area, described below.

VFIO Region capabilities
^^^^^^^^^^^^^^^^^^^^^^^^
The VFIO region information can also include a capabilities list. This list is
similar to a PCI capability list - each entry has a common header that
identifies a capability and where the next capability in the list can be found.
The VFIO capability header format is defined in ``<sys/vfio.h>`` (``struct
vfio_info_cap_header``).

VFIO cap header format
^^^^^^^^^^^^^^^^^^^^^^

+---------+--------+------+
| Name    | Offset | Size |
+=========+========+======+
| id      | 0      | 2    |
+---------+--------+------+
| version | 2      | 2    |
+---------+--------+------+
| next    | 4      | 4    |
+---------+--------+------+

* id is the capability identity.
* version is a capability-specific version number.
* next specifies the offset of the next capability in the capability list. It
  is relative to the beginning of the VFIO region info structure.

VFIO sparse mmap
^^^^^^^^^^^^^^^^

+------------------+----------------------------------+
| Name             | Value                            |
+==================+==================================+
| id               | VFIO_REGION_INFO_CAP_SPARSE_MMAP |
+------------------+----------------------------------+
| version          | 0x1                              |
+------------------+----------------------------------+
| next             | <next>                           |
+------------------+----------------------------------+
| sparse mmap info | VFIO region info sparse mmap     |
+------------------+----------------------------------+

The only capability supported in this version of the protocol is for sparse
mmap. This capability is defined when only a subrange of the region supports
direct access by the client via mmap(). The VFIO sparse mmap area is defined in
``<sys/vfio.h>`` (``struct vfio_region_sparse_mmap_area``).

VFIO region info cap sparse mmap
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
+----------+--------+------+
| Name     | Offset | Size |
+==========+========+======+
| nr_areas | 0      | 4    |
+----------+--------+------+
| reserved | 4      | 4    |
+----------+--------+------+
| offset   | 8      | 8    |
+----------+--------+------+
| size     | 16     | 9    |
+----------+--------+------+
| ...      |        |      |
+----------+--------+------+

* nr_areas is the number of sparse mmap areas in the region.
* offset and size describe a single area that can be mapped by the client.
  There will be nr_areas pairs of offset and size. The offset will be added to
  the base offset given in the VFIO_USER_DEVICE_GET_REGION_INFO to form the
  offset argument of the subsequent mmap() call.

The VFIO sparse mmap area is defined in ``<sys/vfio.h>`` (``struct
vfio_region_info_cap_sparse_mmap``).

VFIO_USER_DEVICE_GET_IRQ_INFO
-----------------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 6                      |
+--------------+------------------------+
| Message size | 32                     |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| IRQ info     | VFIO IRQ info          |
+--------------+------------------------+

This message is sent by the client to the server to query for information about
device interrupt types. The VFIO IRQ info structure is defined in
``<sys/vfio.h>`` (``struct vfio_irq_info``).

VFIO IRQ info format
^^^^^^^^^^^^^^^^^^^^

+-------+--------+---------------------------+
| Name  | Offset | Size                      |
+=======+========+===========================+
| argsz | 16     | 4                         |
+-------+--------+---------------------------+
| flags | 20     | 4                         |
+-------+--------+---------------------------+
|       | +-----+--------------------------+ |
|       | | Bit | Definition               | |
|       | +=====+==========================+ |
|       | | 0   | VFIO_IRQ_INFO_EVENTFD    | |
|       | +-----+--------------------------+ |
|       | | 1   | VFIO_IRQ_INFO_MASKABLE   | |
|       | +-----+--------------------------+ |
|       | | 2   | VFIO_IRQ_INFO_AUTOMASKED | |
|       | +-----+--------------------------+ |
|       | | 3   | VFIO_IRQ_INFO_NORESIZE   | |
|       | +-----+--------------------------+ |
+-------+--------+---------------------------+
| index | 24     | 4                         |
+-------+--------+---------------------------+
| count | 28     | 4                         |
+-------+--------+---------------------------+

* argz is reserved in vfio-user, it is only used in the ioctl() VFIO
  implementation.
* flags defines IRQ attributes:

  * VFIO_IRQ_INFO_EVENTFD indicates the IRQ type can support server eventfd
    signalling.
  * VFIO_IRQ_INFO_MASKABLE indicates that the IRQ type supports the MASK and
    UNMASK actions in a VFIO_USER_DEVICE_SET_IRQS message.
  * VFIO_IRQ_INFO_AUTOMASKED indicates the IRQ type masks itself after being
    triggered, and the client must send an UNMASK action to receive new
    interrupts.
  * VFIO_IRQ_INFO_NORESIZE indicates VFIO_USER_SET_IRQS operations setup
    interrupts as a set, and new subindexes cannot be enabled without disabling
    the entire type.

* index is the index of IRQ type being queried, it is the only field that is
  required to be set in the request message.
* count describes the number of interrupts of the queried type.

VFIO_USER_DEVICE_SET_IRQS
-------------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 7                      |
+--------------+------------------------+
| Message size | 36 + any data          |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| IRQ set      | VFIO IRQ set           |
+--------------+------------------------+

This message is sent by the client to the server to set actions for device
interrupt types. The VFIO IRQ set structure is defined in ``<sys/vfio.h>``
(``struct vfio_irq_set``).

VFIO IRQ info format
^^^^^^^^^^^^^^^^^^^^

+-------+--------+------------------------------+
| Name  | Offset | Size                         |
+=======+========+==============================+
| argsz | 6      | 4                            |
+-------+--------+------------------------------+
| flags | 20     | 4                            |
+-------+--------+------------------------------+
|       | +-----+-----------------------------+ |
|       | | Bit | Definition                  | |
|       | +=====+=============================+ |
|       | | 0   | VFIO_IRQ_SET_DATA_NONE      | |
|       | +-----+-----------------------------+ |
|       | | 1   | VFIO_IRQ_SET_DATA_BOOL      | |
|       | +-----+-----------------------------+ |
|       | | 2   | VFIO_IRQ_SET_DATA_EVENTFD   | |
|       | +-----+-----------------------------+ |
|       | | 3   | VFIO_IRQ_SET_ACTION_MASK    | |
|       | +-----+-----------------------------+ |
|       | | 4   | VFIO_IRQ_SET_ACTION_UNMASK  | |
|       | +-----+-----------------------------+ |
|       | | 5   | VFIO_IRQ_SET_ACTION_TRIGGER | |
|       | +-----+-----------------------------+ |
+-------+--------+------------------------------+
| index | 24     | 4                            |
+-------+--------+------------------------------+
| start | 28     | 4                            |
+-------+--------+------------------------------+
| count | 32     | 4                            |
+-------+--------+------------------------------+
| data  | 36     | variable                     |
+-------+--------+------------------------------+

* argz is reserved in vfio-user, it is only used in the ioctl() VFIO
  implementation.
* flags defines the action performed on the interrupt range. The DATA flags
  describe the data field sent in the message; the ACTION flags describe the
  action to be performed. The flags are mutually exclusive for both sets.

  * VFIO_IRQ_SET_DATA_NONE indicates there is no data field in the request. The
    action is performed unconditionally.
  * VFIO_IRQ_SET_DATA_BOOL indicates the data field is an array of boolean
    bytes. The action is performed if the corresponding boolean is true.
  * VFIO_IRQ_SET_DATA_EVENTFD indicates an array of event file descriptors was
    sent in the message meta-data. These descriptors will be signalled when the
    action defined by the action flags occurs. In AF_UNIX sockets, the
    descriptors are sent as SCM_RIGHTS type ancillary data.
  * VFIO_IRQ_SET_ACTION_MASK indicates a masking event. It can be used with
    VFIO_IRQ_SET_DATA_BOOL or VFIO_IRQ_SET_DATA_NONE to mask an interrupt, or
    with VFIO_IRQ_SET_DATA_EVENTFD to generate an event when the guest masks
    the interrupt.
  * VFIO_IRQ_SET_ACTION_UNMASK indicates an unmasking event. It can be used
    with VFIO_IRQ_SET_DATA_BOOL or VFIO_IRQ_SET_DATA_NONE to unmask an
    interrupt, or with VFIO_IRQ_SET_DATA_EVENTFD to generate an event when the
    guest unmasks the interrupt.
  * VFIO_IRQ_SET_ACTION_TRIGGER indicates a triggering event. It can be used
    with VFIO_IRQ_SET_DATA_BOOL or VFIO_IRQ_SET_DATA_NONE to trigger an
    interrupt, or with VFIO_IRQ_SET_DATA_EVENTFD to generate an event when the
    guest triggers the interrupt.

* index is the index of IRQ type being setup.
* start is the start of the subindex being set.
* count describes the number of sub-indexes being set. As a special case, a
  count of 0 with data flags of VFIO_IRQ_SET_DATA_NONE disables all interrupts
  of the index data is an optional field included when the
  VFIO_IRQ_SET_DATA_BOOL flag is present. It contains an array of booleans
  that specify whether the action is to be performed on the corresponding
  index. It's used when the action is only performed on a subset of the range
  specified.

Not all interrupt types support every combination of data and action flags.
The client must know the capabilities of the device and IRQ index before it
sends a VFIO_USER_DEVICE_SET_IRQ message.

Read and Write Operations
-------------------------

Not all I/O operations between the client and server can be done via direct
access of memory mapped with an mmap() call. In these cases, the client and
server use messages sent over the socket. It is expected that these operations
will have lower performance than direct access.

The client can access device memory with VFIO_USER_REGION_READ and
VFIO_USER_REGION_WRITE requests. These share a common data structure that
appears after the 16 byte message header.

REGION Read/Write Data
^^^^^^^^^^^^^^^^^^^^^^

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| Offset | 16     | 8        |
+--------+--------+----------+
| Region | 24     | 4        |
+--------+--------+----------+
| Count  | 28     | 4        |
+--------+--------+----------+
| Data   | 32     | variable |
+--------+--------+----------+

* Offset into the region being accessed.
* Region is the index of the region being accessed.
* Count is the size of the data to be transferred.
* Data is the data to be read or written.

The server can access guest memory with VFIO_USER_DMA_READ and
VFIO_USER_DMA_WRITE messages. These also share a common data structure that
appears after the 16 byte message header.

DMA Read/Write Data
^^^^^^^^^^^^^^^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| Address | 16     | 8        |
+---------+--------+----------+
| Count   | 24     | 4        |
+---------+--------+----------+
| Data    | 28     | variable |
+---------+--------+----------+

* Address is the area of guest memory being accessed. This address must have
  been exported to the server with a VFIO_USER_DMA_MAP message.
* Count is the size of the data to be transferred.
* Data is the data to be read or written.

Address and count can also be accessed as ``struct iovec`` from ``<sys/uio.h>``.

VFIO_USER_REGION_READ
---------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 8                      |
+--------------+------------------------+
| Message size | 32 + data size         |
+--------------+------------------------+
| Flags Reply  | bit set in reply       |
+--------------+------------------------+
| Read info    | REGION read/write data |
+--------------+------------------------+

This request is sent from the client to the server to read from device memory.
In the request messages, there will be no data, and the count field will be the
amount of data to be read. The reply will include the data read, and its count
field will be the amount of data read.

VFIO_USER_REGION_WRITE
----------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 9                      |
+--------------+------------------------+
| Message size | 32 + data size         |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| Write info   | REGION read write data |
+--------------+------------------------+

This request is sent from the client to the server to write to device memory.
The request message will contain the data to be written, and its count field
will contain the amount of write data. The count field in the reply will be
zero.

VFIO_USER_DMA_READ
------------------

Message format
^^^^^^^^^^^^^^

+--------------+---------------------+
| Name         | Value               |
+==============+=====================+
| Device ID    | <ID>                |
+--------------+---------------------+
| Message ID   | <ID>                |
+--------------+---------------------+
| Command      | 10                  |
+--------------+---------------------+
| Message size | 28 + data size      |
+--------------+---------------------+
| Flags Reply  | bit set in reply    |
+--------------+---------------------+
| DMA info     | DMA read/write data |
+--------------+---------------------+

This request is sent from the server to the client to read from guest memory.
In the request messages, there will be no data, and the count field will be the
amount of data to be read. The reply will include the data read, and its count
field will be the amount of data read.

VFIO_USER_DMA_WRITE
-------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 11                     |
+--------------+------------------------+
| Message size | 28 + data size         |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+
| DMA info     | DMA read/write data    |
+--------------+------------------------+

This request is sent from the server to the client to write to guest memory.
The request message will contain the data to be written, and its count field
will contain the amount of write data. The count field in the reply will be
zero.

VFIO_USER_VM_INTERRUPT
----------------------

Message format
^^^^^^^^^^^^^^

+----------------+------------------------+
| Name           | Value                  |
+================+========================+
| Device ID      | <ID>                   |
+----------------+------------------------+
| Message ID     | <ID>                   |
+----------------+------------------------+
| Command        | 12                     |
+----------------+------------------------+
| Message size   | 24                     |
+----------------+------------------------+
| Flags          | Reply bit set in reply |
+----------------+------------------------+
| Interrupt info | <interrupt>            |
+----------------+------------------------+

This request is sent from the server to the client to signal the device has
raised an interrupt.

Interrupt info format
^^^^^^^^^^^^^^^^^^^^^

+----------+--------+------+
| Name     | Offset | Size |
+==========+========+======+
| Index    | 16     | 4    |
+----------+--------+------+
| Subindex | 20     | 4    |
+----------+--------+------+

* Index is the interrupt index; it is the same value used in VFIO_USER_SET_IRQS.
* Subindex is relative to the index, e.g., the vector number used in PCI MSI/X
  type interrupts.

VFIO_USER_DEVICE_RESET
----------------------

Message format
^^^^^^^^^^^^^^

+--------------+------------------------+
| Name         | Value                  |
+==============+========================+
| Device ID    | <ID>                   |
+--------------+------------------------+
| Message ID   | <ID>                   |
+--------------+------------------------+
| Command      | 13                     |
+--------------+------------------------+
| Message size | 16                     |
+--------------+------------------------+
| Flags        | Reply bit set in reply |
+--------------+------------------------+

This request is sent from the client to the server to reset the device.

Appendices
==========

Unused VFIO ioctl() commands
----------------------------

The following commands must be handled by the client and not sent to the server:

* VFIO_GET_API_VERSION
* VFIO_CHECK_EXTENSION
* VFIO_SET_IOMMU
* VFIO_GROUP_GET_STATUS
* VFIO_GROUP_SET_CONTAINER
* VFIO_GROUP_UNSET_CONTAINER
* VFIO_GROUP_GET_DEVICE_FD
* VFIO_IOMMU_GET_INFO

However, once support for live migration for VFIO devices is finalized some
of the above commands might have to be handled by the client. This will be
addressed in a future protocol version.

Live Migration
--------------
Currently live migration is not supported for devices passed through via VFIO,
therefore it is not supported for VFIO-over-socket, either. This is being
actively worked on in the "Add migration support for VFIO devices" (v25) patch
series.

VFIO groups and containers
^^^^^^^^^^^^^^^^^^^^^^^^^^

The current VFIO implementation includes group and container idioms that
describe how a device relates to the host IOMMU. In the VFIO over socket
implementation, the IOMMU is implemented in SW by the client, and isn't visible
to the server. The simplest idea is for the client is to put each device into
its own group and container.
