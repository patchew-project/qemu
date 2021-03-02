=================================
Confidential Guest Live Migration
=================================

When migrating regular QEMU guests, QEMU reads the guest's RAM and sends it
over to the migration target host, where QEMU there writes it into the target
guest's RAM and starts the VM.  This mechanism doesn't work when the guest
memory is encrypted or QEMU is prevented from reading it in another way.

In order to support live migration in such scenarios, QEMU relies on an
in-guest migration helper which can securely extract RAM content from the
guest in order to send it to the target.  The migration helper is implemented as
part of the VM's firmware in OVMF.


Migration flow
==============

Source VM
---------

The source VM is started with an extra auxiliary vcpu which is not listed in the
ACPI tables.  OVMF uses this vcpu and starts a dedicated migration helper on it;
the migration helper simply waits for commands from QEMU.  When migration starts
using the ``migrate`` command, QEMU starts saving the state of the different
devices.  When it reaches saving RAM pages, it'll check for each page whether it
is encrypted or not; for encrypted pages, it'll send a command to the migration
helper to extract the given page.  The migration helper receives this command,
reads the page content, encrypts it with a transport key, and returns the
newly-encrypted page to QEMU.  QEMU saves those pages to the outgoing migration
stream using a new page flag ``RAM_SAVE_FLAG_GUEST_ENCRYPTED_PAGE``.

When QEMU reaches the last stage of RAM migration, it stops the source VM to
avoid dirtying the last pages of RAM.  However, the auxiliary vcpu must be kept
running so the migration helper can still extract pages from the guest memory.

Target VM
---------

Usually QEMU migration target VMs are started with the ``-incoming``
command-line option which starts the VM paused.  However, in order to migrate
confidential guests we must have the migration helper running inside the guest;
in such a case, we start the target with a special ``-fw_cfg`` value that tells
OVMF to enter a CPU dead loop on all vcpus except the auxiliary vcpu, which runs
the migration helper.  After this short "boot" completes, QEMU can switch to the
"migration incoming" mode; we do that with the new ``start-migrate-incoming``
QMP command that makes the target VM listen for incoming migration connections.

QEMU will load the state of VM devices as it arrives from the incoming migration
stream.  When it encounters a RAM page with the
``RAM_SAVE_FLAG_GUEST_ENCRYPTED_PAGE`` flag, it will send its
transport-encrypted content and guest physical address to the migration helper.
The migration helper running inside the guest will decrypt the page using the
transport key and place the content in memory (again, that memory page is not
accessible to host due to the confidential guest properties; for example, in SEV
it is hardware-encrypted with a VM-specific key).


Usage
=====

In order to start the source and target VMs with auxiliary CPUs, the auxcpus=
option must be passed to ``-smp`` . For example::

    # ${QEMU} -smp 5,auxcpus=1 ...

This command starts a VM with 5 vcpus of which 4 are main vcpus (available for
the guest OS) and 1 is auxliary vcpu.

Moreover, in both the source and target we need to instruct OVMF to start the
migration helper running in the auxiliary vcpu.  This is achieved using the
following command-line option::

    # ${QEMU} -fw_cfg name=opt/ovmf/PcdSevIsMigrationHelper,string=0 ...

In the target VM we need to add another ``-fw_cfg`` entry to instruct OVMF to
start only the migration helepr, which will wait for incoming pages (the target
cannot be started with ``-incoming`` because that option completely pauses the
VM, not allowing the migration helper to run). Because the migration helper must
be running when the incoming RAM pages are received, starting the target VM with
the ``-incoming`` option doesn't work (with that option, the VM doesn't start
executing).  Instead, start the target VM without ``-incoming`` but with the
following option::

    # ${QEMU} -fw_cfg name=opt/ovmf/PcdSevIsMigrationTarget,string=1 ...

After the VM boots into the migration helper, we instruct QEMU to listen for
incoming migration connections by sending the following QMP command::

    { "execute": "start-migrate-incoming",
      "arguments": { "uri": "tcp:0.0.0.0:6666" } }

Now that the target is ready, we instruct the source VM to start migrating its
state using the regular ``migrate`` QMP command, supplying the target VMs
listening address::

    { "execute": "migrate",
      "arguments": { "uri": "tcp:192.168.111.222:6666" } }


Implementation details
======================

Migration helper <-> QEMU communication
---------------------------------------

The migration helper is running inside the guest (implemented as part of OVMF).
QEMU communicates with it using a mailbox protocol over two shared (unencrypted)
4K RAM pages.

The first page contains a ``SevMigHelperCmdParams`` struct at offset 0x0
(``cmd_params``) and a ``MigrationHelperHeader`` struct at offset 0x800
(``io_hdr``).  The second page (``io_page``) is dedicated for encrypted page
content.

In order to save a confidential RAM page, QEMU will fill the ``cmd_params``
struct to indicate the SEV_MIG_HELPER_CMD_ENCRYPT command and the requested gpa
(guest physical address), and then set the ``go`` field to 1.  Meanwhile the
migration helper waits for the ``go`` field to become non-zero; after it notices
``go`` is 1 it'll read the gpa, read the content of the relevant page from the
guest's memory, encrypt it with the transport key, and store the
transport-encrypted page in the the ``io_page``.  Additional envelope data like
encryption IV and other fields are stored in ``io_hdr``.  After the migration is
done writing to ``io_page`` and ``io_hdr``, it sets the ``done`` field to 1.  At
this point QEMU notices that the migration helper is done and can continue its
part, which is saving the header and page to the outgoing migration stream.

Similar process is used when loading a confidential RAM from the incoming
migration stream.  QEMU reads the header and the encrypted page from the stream,
and copies them into the shared areas ``io_hdr`` and ``io_page`` respectably.
It then fills the ``cmd_params`` struct to indicate the
SEV_MIG_HELPER_CMD_DECRYPT command and the gpa, and sets ``go`` to 1.  The
migration helper will notice the command, will decrypt the page using the
transport key and will place the decrypted content in the requetsed gpa, and set
``done`` to 1 to allow QEMU to continue processing the next item in the incoming
migration stream.

Shared pages address discovery
------------------------------
In the current implementation the address of the two shared pages is hard-coded
in both OVMF and QEMU.  We plan for OVMF to expose this address via its GUIDed
table and let QEMU discover it using ``pc_system_ovmf_table_find()``.
