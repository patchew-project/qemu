"""
QEMU development and testing library.

This library provides a few high-level classes for driving QEMU from a
test suite, not intended for production use.

- QEMUMachine: Configure and Boot a QEMU VM
 - QEMUQtestMachine: VM class, with a qtest socket.

- QEMUMonitorProtocol: Connect to, send/receive QMP messages.
- QEMUQtestProtocol: Connect to, send/receive qtest message.

- list_accel: List available accelerators
- kvm_available: Probe for KVM support
- tcg_available: Probe for TCG support
"""

# Copyright (C) 2020 John Snow for Red Hat Inc.
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#  Fam Zheng <fam@euphon.net>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

from .accel import (
    list_accel,
    kvm_available,
    tcg_available,
)

from .qmp import (
    QEMUMonitorProtocol,
)

from .machine import (
    QEMUMachine,
)

from .qtest import (
    QEMUQtestProtocol,
    QEMUQtestMachine,
)

__all__ = (
    'list_accel',
    'kvm_available',
    'tcg_available',
    'QEMUMonitorProtocol',
    'QEMUMachine',
    'QEMUQtestProtocol',
    'QEMUQtestMachine',
)
