.. SPDX-License-Identifier: GPL-2.0-or-later

Secure IPL Command Line Options
===============================

The s390-ccw-virtio machine type supports secure IPL. These parameters allow users
to provide certificates and enable secure IPL directly via the command line.

Providing Certificates
----------------------

The certificate store can be populated by supplying a list of X.509 certificate file
paths or directories containing certificate files on the command-line:

Note: certificate files must have a .pem extension.

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
                               boot-certs.0.path=/.../qemu/certs, \
                               boot-certs.1.path=/another/path/cert.pem ...

Enabling Secure IPL
-------------------

Secure IPL is enabled by explicitly setting ``secure-boot=on``; if not specified,
secure boot is considered off.

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on|off


IPL Modes
=========

The concept of IPL Modes are introduced to differentiate between the IPL configurations.
These modes are mutually exclusive and enabled based on specific combinations of
the ``secure-boot`` and ``boot-certs`` options on the QEMU command line.

Normal Mode
-----------

The absence of both certificates and the ``secure-boot`` option will attempt to
IPL a guest without secure IPL operations. No checks are performed, and no
warnings/errors are reported.  This is the default mode, and can be explicitly
enabled with ``secure-boot=off``.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio ...

Audit Mode
----------

With *only* the presence of certificates in the store, it is assumed that secure
boot operations should be performed with errors reported as warnings. As such,
the secure IPL operations will be performed, and any errors that stem from these
operations will report a warning via the SCLP console.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
                               boot-certs.0.path=/.../qemu/certs, \
                               boot-certs.1.path=/another/path/cert.pem ...

Secure Mode
-----------

With *both* the presence of certificates in the store and the ``secure-boot=on``
option, it is understood that secure boot should be performed with errors
reported and boot will abort.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
                               secure-boot=on, \
                               boot-certs.0.path=/.../qemu/certs, \
                               boot-certs.1.path=/another/path/cert.pem ...
