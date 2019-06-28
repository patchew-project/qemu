# Utilities for using QEMU accelerators on tests.
#
# Copyright (c) 2019 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu import QEMUMachine
from qemu import kvm_available

def list_accel(qemu_bin):
    """
    List accelerators enabled in the binary.

    :param qemu_bin: path to the QEMU binary.
    :type qemu_bin: str
    :returns: list of accelerator names.
    :rtype: list
    """
    vm = QEMUMachine(qemu_bin)
    vm.set_qmp_monitor(disabled=True)
    vm.add_args('-accel', 'help')
    vm.launch()
    vm.wait()
    if vm.exitcode() != 0:
        raise Exception("Failed to get the accelerators in %s" % qemu_bin)
    lines = vm.get_log().splitlines()
    # skip first line which is the output header.
    return [l for l in lines[1:] if l]

def _tcg_avail_checker(qemu_bin):
    # checks TCG is enabled in the binary only.
    return 'tcg' in list_accel(qemu_bin)

def _kvm_avail_checker(qemu_bin):
    # checks KVM is present in the host as well as enabled in the binary.
    return kvm_available() and "kvm" in list_accel(qemu_bin)

_CHECKERS = {"tcg": _tcg_avail_checker, "kvm": _kvm_avail_checker}

def is_accel_available(accel, qemu_bin):
    """
    Check the accelerator is available (enabled in the binary as well as
    present on host).

    :param accel:  accelerator's name.
    :type accel: str
    :param qemu_bin: path to the QEMU binary.
    :type qemu_bin: str
    :returns: True if accelerator is available, False otherwise.
    :rtype: boolean
    """
    checker = _CHECKERS.get(accel, None)
    if checker:
        return checker(qemu_bin)
    raise Exception("Availability checker not implemented for %s accelerator." %
                    accel)
