.. SPDX-License-Identifier: GPL-2.0-or-later

Secure IPL Command Line Options
===============================

New parameters have been introduced to s390-ccw-virtio machine type option
to support secure IPL. These parameters allow users to provide certificates
and enable secure IPL directly via the command line.

Providing Certificates
----------------------

The certificate store can be populated by supplying a list of certificate file
paths or directories on the command-line:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio, \
                               boot-certs.0.path=/.../qemu/certs, \
                               boot-certs.1.path=/another/path/cert.pem ...
