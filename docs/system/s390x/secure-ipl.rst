.. SPDX-License-Identifier: GPL-2.0-or-later

s390 Secure IPL
===============

Secure IPL, also known as secure boot, enables s390-ccw virtual machines to
verify the integrity of guest kernels.

For technical details of this feature, see ``docs/specs/s390x-secure-ipl.rst``.

This document explains how to use secure IPL with s390x in QEMU. It covers
new command line options for providing certificates and enabling secure IPL,
the different IPL modes (Normal, Audit, and Secure), and system requirements.

A quickstart guide is provided to demonstrate how to generate certificates,
sign images, and start a guest in Secure Mode.


Secure IPL Command Line Options
===============================

New parameters have been introduced to s390-ccw-virtio machine type option
to support secure IPL. These parameters allow users to provide certificates
and enable secure IPL directly via the command line.

Providing Certificates
----------------------

The certificate store can be populated by supplying a comma-delimited list of
certificates on the command-line:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
    boot-certificates=/.../qemu/certs:/another/path/cert.pem

Enabling Secure IPL
-------------------

Different IPL modes may be toggled with the following command line option:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on|off

Additionally, the provision of certificates affect the mode.


IPL Modes
=========

Normal Mode
-----------

The absence of both certificates and the ``secure-boot`` option will attempt to
IPL a guest without secure IPL operations. No checks are performed, and no
warnings/errors are reported.  This is the default mode, and can be explicitly
enabled with ``secure-boot=off``.


Audit Mode
----------

With *only* the presence of certificates in the store, it is assumed that secure
boot operations should be performed with errors reported as warnings. As such,
the secure IPL operations will be performed, and any errors that stem from these
operations will report a warning via the SCLP console.


Secure Mode
-----------

With *both* the presence of certificates in the store and the ``secure-boot=on``
option, it is understood that secure boot should be performed with errors
reported and boot will abort.


Constraints
===========

The following constraints apply when attempting to secure IPL an s390 guest:

- z16 CPU model
- certificates must be in X.509 PEM format
- only sha256 encryption is supported
- only support for SCSI scheme of virtio-blk/virtio-scsi devices
- a boot device must be specified
- any unsupported devices (e.g., ECKD and VFIO) or non-eligible devices (e.g.,
  Net) will cause the entire boot process terminating early with an error
  logged to the console.


Secure IPL Quickstart
=====================

Build QEMU with gnutls enabled
-------------------------------

.. code-block:: shell

    ./configure … --enable-gnutls

Generate certificate (e.g. via certtool)
----------------------------------------

A private key is required before generating a certificate. This key must be keypt secure
and confidential.

Use an RSA private key for signing.

.. code-block:: shell

    certtool --generate-privkey > key.pem

A self-signed certificate requires the organization name. Use the ``cert.info`` template
to pre-fill values and avoid interactive prompts from certtool.

.. code-block:: shell

    cat > cert.info <<EOF
    cn = "My Name"
    expiration_days = 36500
    cert_signing_key
    EOF

    certtool --generate-self-signed \
             --load-privkey key.pem \
             --template cert.info \
             --hash=SHA256 \
             --outfile cert.pem

Sign Images (e.g. via sign-file)
--------------------------------

- signing must be performed on a KVM guest filesystem
- sign-file script used in the example below is located within the kernel source
  repo

.. code-block:: shell

    ./sign-file sha256 key.pem cert.pem /boot/vmlinuz-…
    ./sign-file sha256 key.pem cert.pem /usr/lib/s390-tools/stage3.bin

Run zipl with secure boot enabled
---------------------------------

.. code-block:: shell

    zipl --secure 1 -V

Start Guest with Cmd Options
----------------------------

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on,boot-certificates=cert.pem ...
