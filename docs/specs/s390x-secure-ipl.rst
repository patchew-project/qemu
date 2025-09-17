.. SPDX-License-Identifier: GPL-2.0-or-later

s390 Secure IPL
===============

Secure IPL (a.k.a. secure boot) enables s390-ccw virtual machines to
leverage qcrypto libraries and z/Architecture emulations to verify the
integrity of signed kernels. The qcrypto libraries are used to perform
certificate validation and signature-verification, whereas the
z/Architecture emulations are used to ensure secure IPL data has not
been tampered with, convey data between QEMU and userspace, and set up
the relevant secure IPL data structures with verification results.

To find out more about using this feature, see ``docs/system/s390x/secure-ipl.rst``.

Note that "userspace" will refer to the s390-ccw BIOS unless stated
otherwise.

Both QEMU and userspace work in tandem to perform secure IPL. The Secure
Loading Attributes Facility (SCLAF) is used to check the Secure Code
Loading Attribute Block (SCLAB) and ensure that secure IPL data has not
been tampered with. DIAGNOSE 'X'320' is invoked by userspace to query
the certificate store info and retrieve specific certificates from QEMU.
DIAGNOSE 'X'508' is used by userspace to leverage qcrypto libraries to
perform signature-verification in QEMU. Lastly, userspace generates and
appends an IPL Information Report Block (IIRB) at the end of the IPL
Parameter Block, which is used by the kernel to store signed and
verified entries.

The logical steps are as follows:

- Userspace reads data payload from disk (e.g. stage3 boot loader, kernel)
- Userspace checks the validity of the SCLAB
- Userspace invokes DIAG 508 subcode 1 and provides it the payload
- QEMU handles DIAG 508 request by reading the payload and retrieving the
  certificate store
- QEMU DIAG 508 utilizes qcrypto libraries to perform signature-verification on
  the payload, attempting with each cert in the store (until success or exhausted)
- QEMU DIAG 508 returns:

  - success: index of cert used to verify payload
  - failure: error code

- Userspace responds to this operation:

  - success: retrieves cert from store via DIAG 320 using returned index
  - failure: reports with warning (audit mode), aborts with error (secure mode)

- Userspace appends IIRB at the end of the IPLB
- Userspace kicks off IPL

More information regarding the respective DIAGNOSE commands and IPL data
structures are outlined within this document.


s390 Certificate Store and Functions
====================================

s390 Certificate Store
----------------------

A certificate store is implemented for s390-ccw guests to retain within
memory all certificates provided by the user via the command-line, which
are expected to be stored somewhere on the host's file system. The store
will keep track of the number of certificates, their respective size,
and a summation of the sizes.

Note: A maximum of 64 certificates are allowed to be stored in the certificate store.

DIAGNOSE function code 'X'320' - Certificate Store Facility
-----------------------------------------------------------

DIAGNOSE 'X'320' is used to provide support for userspace to directly
query the s390 certificate store. Userspace may be the s390-ccw BIOS or
the guest kernel.

Subcode 0 - query installed subcodes
    Returns a 256-bit installed subcodes mask (ISM) stored in the installed
    subcodes block (ISB). This mask indicates which sucodes are currently
    installed and available for use.

Subcode 1 - query verification certificate storage information
    Provides the information required to determine the amount of memory needed to
    store one or more verification-certificates (VCs) from the certificate store (CS).

    Upon successful completion, this subcode returns various storage size values for
    verification-certificate blocks (VCBs).

    The output is returned in the verification-certificate-storage-size block (VCSSB).
    A VCSSB length of 4 indicates that no certificates are available in the CS.

Subcode 2 - store verification certificates
    Provides VCs that are in the certificate store.

    The output is provided in a VCB, which includes a common header followed by zero
    or more verification-certificate entries (VCEs).

    The first-VC index and last-VC index fields of VCB specify the range of VCs
    to be stored by subcode 2. Stored count and remained count fields specify the
    number of VCs stored and could not be stored in the VCB due to insufficient
    storage specified in the VCB input length field.

    VCE contains various information of a VC from the CS.


Secure IPL Data Structures, Facilities, and Functions
=====================================================

DIAGNOSE function code 'X'508' - KVM IPL extensions
---------------------------------------------------

DIAGNOSE 'X'508' is reserved for KVM guest use in order to facilitate
communication of additional IPL operations that cannot be handled by userspace,
such as signature verification for secure IPL.

If the function code specifies 0x508, KVM IPL extension functions are performed.
These functions are meant to provide extended functionality for s390 guest boot
that requires assistance from QEMU.

Subcode 0 - query installed subcodes
    Returns a 64-bit mask indicating which subcodes are supported.

Subcode 1 - perform signature verification
    Perform signature-verification on a signed component, using certificates
    from the certificate store and leveraging qcrypto libraries to perform
    this operation.


IPL Information Report Block
----------------------------

The IPL Parameter Block (IPLPB), utilized for IPL operation, is extended with an
IPL Information Report Block (IIRB), which contains the results from secure IPL
operations such as:

* component data
* verification results
* certificate data

The guest kernel will inspect the IIRB and build the keyring.


Secure Code Loading Attributes Facility
---------------------------------

The Secure Code Loading Attributes Facility (SCLAF) enhances system security during the
IPL by enforcing additional verification rules.

When SCLAF is available, its behavior depends on the IPL mode. It introduces verification
of both signed and unsigned components to help ensure that only authorized code is loaded
during the IPL process. Any errors detected by SCLAF are reported in the IIRB.

Unsigned components are restricted to load addresses at or above absolute storage address
``0x2000``.

Signed components must include a Secure Code Loading Attribute Block (SCLAB), which is
appended at the very end of the component. The SCLAB defines security attributes for
handling the signed code. Specifically, it may:

* Provide direction on how to process the rest of the component.

* Provide further validation of information on where to load the signed binary code
  from the load device.

* Specify where to start the execution of the loaded OS code.
