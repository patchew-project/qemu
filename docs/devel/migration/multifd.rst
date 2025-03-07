Multifd
=======

Multifd is the name given for the migration capability that enables
data transfer using multiple threads. Multifd supports all the
transport types currently in use with migration (inet, unix, vsock,
fd, file).

Usage
-----

On both source and destination, enable the ``multifd`` capability:

    ``migrate_set_capability multifd on``

Define a number of channels to use (default is 2, but 8 usually
provides best performance).

    ``migrate_set_parameter multifd-channels 8``

Restrictions
------------

For migration to a file, support is conditional on the presence of the
mapped-ram capability, see `mapped-ram`.

Snapshots are currently not supported.

`postcopy` migration is currently not supported.

Components
----------

Multifd consists of:

- A client that produces the data on the migration source side and
  consumes it on the destination. Currently the main client code is
  ram.c, which selects the RAM pages for migration;

- A shared data structure (``MultiFDSendData``), used to transfer data
  between multifd and the client. On the source side, this structure
  is further subdivided into payload types (``MultiFDPayload``);

- An API operating on the shared data structure to allow the client
  code to interact with multifd;

  - ``multifd_send/recv()``: Transfers work to/from the channels.

  - ``multifd_*payload_*`` and ``MultiFDPayloadType``: Support
    defining an opaque payload. The payload is always wrapped by
    ``MultiFD*Data``.

  - ``multifd_send_data_*``: Used to manage the memory for the shared
    data structure.

  - ``multifd_*_sync_main()``: See :ref:`synchronization` below.

- A set of threads (aka channels, due to a 1:1 mapping to QIOChannels)
  responsible for doing I/O. Each multifd channel supports callbacks
  (``MultiFDMethods``) that can be used for fine-grained processing of
  the payload, such as compression and zero page detection.

- A packet which is the final result of all the data aggregation
  and/or transformation. The packet contains: a *header* with magic and
  version numbers and flags that inform of special processing needed
  on the destination; a *payload-specific header* with metadata referent
  to the packet's data portion, e.g. page counts; and a variable-size
  *data portion* which contains the actual opaque payload data.

  Note that due to historical reasons, the terminology around multifd
  packets is inconsistent.

  The `mapped-ram` feature ignores packets entirely.

Operation
---------

The multifd channels operate in parallel with the main migration
thread. The transfer of data from a client code into multifd happens
from the main migration thread using the multifd API.

The interaction between the client code and the multifd channels
happens in the ``multifd_send()`` and ``multifd_recv()``
methods. These are reponsible for selecting the next idle channel and
making the shared data structure containing the payload accessible to
that channel. The client code receives back an empty object which it
then uses for the next iteration of data transfer.

The selection of idle channels is simply a round-robin over the idle
channels (``!p->pending_job``). Channels wait at a semaphore and once
a channel is released it starts operating on the data immediately.

Aside from eventually transmitting the data over the underlying
QIOChannel, a channel's operation also includes calling back to the
client code at pre-determined points to allow for client-specific
handling such as data transformation (e.g. compression), creation of
the packet header and arranging the data into iovs (``struct
iovec``). Iovs are the type of data on which the QIOChannel operates.

A high-level flow for each thread is:

Migration thread:

#. Populate shared structure with opaque data (e.g. ram pages)
#. Call ``multifd_send()``

   #. Loop over the channels until one is idle
   #. Switch pointers between client data and channel data
   #. Release channel semaphore
#. Receive back empty object
#. Repeat

Multifd thread:

#. Channel idle
#. Gets released by ``multifd_send()``
#. Call ``MultiFDMethods`` methods to fill iov

   #. Compression may happen
   #. Zero page detection may happen
   #. Packet is written
   #. iov is written
#. Pass iov into QIOChannel for transferring (I/O happens here)
#. Repeat

The destination side operates similarly but with ``multifd_recv()``,
decompression instead of compression, etc. One important aspect is
that when receiving the data, the iov will contain host virtual
addresses, so guest memory is written to directly from multifd
threads.

About flags
-----------
The main thread orchestrates the migration by issuing control flags on
the migration stream (``QEMU_VM_*``).

The main memory is migrated by ram.c and includes specific control
flags that are also put on the main migration stream
(``RAM_SAVE_FLAG_*``).

Multifd has its own set of flags (``MULTIFD_FLAG_*``) that are
included into each packet. These may inform about properties such as
the compression algorithm used if the data is compressed.

.. _synchronization:

Synchronization
---------------

Data sent through multifd may arrive out of order and with different
timing. Some clients may also have synchronization requirements to
ensure data consistency, e.g. the RAM migration must ensure that
memory pages received by the destination machine are ordered in
relation to previous iterations of dirty tracking.

Some cleanup tasks such as memory deallocation or error handling may
need to happen only after all channels have finished sending/receiving
the data.

Multifd provides the ``multifd_send_sync_main()`` and
``multifd_recv_sync_main()`` helpers to synchronize the main migration
thread with the multifd channels. In addition, these helpers also
trigger the emission of a sync packet (``MULTIFD_FLAG_SYNC``) which
carries the synchronization command to the remote side of the
migration.

After the channels have been put into a wait state by the sync
functions, the client code may continue to transmit additional data by
issuing ``multifd_send()`` once again.

Note:

- the RAM migration does, effectively, a global synchronization by
  chaining a call to ``multifd_send_sync_main()`` with the emission of a
  flag on the main migration channel (``RAM_SAVE_FLAG_MULTIFD_FLUSH``)
  which in turn causes ``multifd_recv_sync_main()`` to be called on the
  destination.

  There are also backward compatibility concerns expressed by
  ``multifd_ram_sync_per_section()`` and
  ``multifd_ram_sync_per_round()``. See the code for detailed
  documentation.

- the `mapped-ram` feature has different requirements because it's an
  asynchronous migration (source and destination not migrating at the
  same time). For that feature, only the sync between the channels is
  relevant to prevent cleanup to happen before data is completely
  written to (or read from) the migration file.

Data transformation
-------------------

The ``MultiFDMethods`` structure defines callbacks that allow the
client code to perform operations on the data at key points. These
operations could be client-specific (e.g. compression), but also
include a few required steps such as moving data into an iovs. See the
struct's definition for more detailed documentation.

Historically, the only client for multifd has been the RAM migration,
so the ``MultiFDMethods`` are pre-registered in two categories,
compression and no-compression, with the latter being the regular,
uncompressed ram migration.

Zero page detection
+++++++++++++++++++

The migration without compression has a further specificity of
possibly doing zero page detection. It involves doing the detection of
a zero page directly in the multifd channels instead of beforehand on
the main migration thread (as it's been done in the past). This is the
default behavior and can be disabled with:

    ``migrate_set_parameter zero-page-detection legacy``

or to disable zero page detection completely:

    ``migrate_set_parameter zero-page-detection none``

Compression
+++++++++++

.. toctree::
   :maxdepth: 1

   qpl-compression
   uadk-compression
   qatzip-compression

Error handling
--------------

Any part of multifd code can be made to exit by setting the
``exiting`` atomic flag of the multifd state. Whenever a multifd
channel has an error, it should break out of its loop, set the flag to
indicate other channels to exit as well and set the migration error
with ``migrate_set_error()``.

For clean exiting (triggered from outside the channels), the
``multifd_send|recv_terminate_threads()`` functions set the
``exiting`` flag and additionally release any channels that may be
idle or waiting for a sync.

Code structure
--------------

Multifd code is divided into:

The main file containing the core routines

- multifd.c

RAM migration

- multifd-nocomp.c (nocomp, for "no compression")
- multifd-zero-page.c
- ram.c (also involved in non-multifd migrations & snapshots)

Compressors

- multifd-uadk.c
- multifd-qatzip.c
- multifd-zlib.c
- multifd-qpl.c
- multifd-zstd.c
