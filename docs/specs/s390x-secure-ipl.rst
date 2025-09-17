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
