.. SPDX-License-Identifier: GPL-2.0-or-later

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
