'nitro-enclave' virtual machine (``nitro-enclave``)
===================================================

``nitro-enclave`` is a machine type which emulates an ``AWS nitro enclave``
virtual machine. `AWS nitro enclaves`_ is an `Amazon EC2`_ feature that allows
creating isolated execution environments, called enclaves, from Amazon EC2
instances which are used for processing highly sensitive data. Enclaves have
no persistent storage and no external networking. The enclave VMs are based
on Firecracker microvm with a vhost-vsock device for communication with the
parent EC2 instance that spawned it and a Nitro Secure Module (NSM) device
for cryptographic attestation. The parent instance VM always has CID 3 while
the enclave VM gets a dynamic CID. Enclaves use an EIF (`Enclave Image Format`_)
file which contains the necessary kernel, cmdline and ramdisk(s) to boot.

In QEMU, ``nitro-enclave`` is a machine type based on ``microvm`` similar to how
``AWS nitro enclaves`` are based on ``Firecracker``. This is useful for local
testing of EIF images using QEMU instead of running real AWS Nitro Enclaves
which can be difficult for debugging due to its roots in security.

.. _AWS nitro enlaves: https://docs.aws.amazon.com/enclaves/latest/user/nitro-enclave.html
.. _Amazon EC2: https://aws.amazon.com/ec2/
.. _Enclave Image Format: https://github.com/aws/aws-nitro-enclaves-image-format


Limitations
-----------

AWS nitro enclave emulation support is not complete yet:

- Although support for the vhost-vsock device is implemented, standalone
nitro-enclave VMs cannot be run right now as nitro-enclave VMs communicate
with a parent instance VM with CID 3. So another VM with CID 3 must be run
with necessary vsock communication support.
- Enclaves also have a Nitro Secure Module (NSM) device which is not implemented
yet.


Using the nitro-enclave machine type
------------------------------

Machine-specific options
~~~~~~~~~~~~~~~~~~~~~~~~

It supports the following machine-specific options:

- nitro-enclave.guest-cid=uint32_t (required) (Set nitro enclave VM's CID)


Running a nitro-enclave VM
~~~~~~~~~~~~~~~~~~~~~~~~~~

A nitro-enclave VM can be run using the following command where ``hello.eif`` is
an EIF image you would use to spawn a real AWS nitro enclave virtual machine:

  $ sudo qemu-system-x86_64 -M nitro-enclave,guest-cid=8 \
     -enable-kvm -cpu host -m 512m \
     -kernel hello.eif \
     -nographic
