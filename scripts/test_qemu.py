import sys
import os
import glob
import unittest
import shutil
import tempfile
import subprocess

import qemu
import qmp.qmp


class QEMUMachineProbeError(Exception):
    """
    Exception raised when a probe a fails to be deterministic
    """


def get_built_qemu_binaries(root_dir=None):
    """
    Attempts to find QEMU binaries in a tree

    If root_dir is None, it will attempt to find the binaries at the
    source tree.  It's possible to override it by setting the environment
    variable QEMU_ROOT_DIR.
    """
    if root_dir is None:
        src_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        root_dir = os.environ.get("QEMU_ROOT_DIR", src_dir)
    binaries = glob.glob(os.path.join(root_dir, '*-softmmu/qemu-system-*'))
    if 'win' in sys.platform:
        bin_filter = lambda x: x.endswith(".exe")
    else:
        bin_filter = lambda x: not x.endswith(".exe")
    return [_ for _ in binaries if bin_filter(_)]


def subprocess_dev_null(mode='w'):
    """
    A portable null file object suitable for use with the subprocess module
    """
    if hasattr(subprocess, 'DEVNULL'):
        return subprocess.DEVNULL
    else:
        return open(os.path.devnull, mode)


def qmp_execute(binary_path, qmp_command):
    """
    Executes a QMP command on a given QEMU binary

    Useful for one-off execution of QEMU binaries to get runtime
    information.

    @param binary_path: path to a QEMU binary
    @param qmp_command: the QMP command
    @note: passing arguments to the QMP command is not supported at
           this time.
    """
    try:
        tempdir = tempfile.mkdtemp()
        monitor_socket = os.path.join(tempdir, 'monitor.sock')
        args = [binary_path, '-nodefaults', '-machine', 'none',
                '-nographic', '-S', '-qmp', 'unix:%s' % monitor_socket]
        monitor = qmp.qmp.QEMUMonitorProtocol(monitor_socket, True)
        try:
            qemu_proc = subprocess.Popen(args,
                                         stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess_dev_null(),
                                         universal_newlines=True)
        except OSError:
            return None
        monitor.accept()
        res = monitor.cmd(qmp_command)
        monitor.cmd("quit")
        qemu_proc.wait()
        monitor.close()
        return res.get("return", None)
    finally:
        shutil.rmtree(tempdir)


def qemu_bin_probe_arch(binary_path):
    """
    Probes the architecture from the QEMU binary

    @returns: either the probed arch or None
    @rtype: str or None
    @raises: QEMUMachineProbeError
    """
    res = qmp_execute(binary_path, "query-target")
    if res is None:
        raise QEMUMachineProbeError('Failed to probe the QEMU arch by querying'
                                    ' the target of binary "%s"' % binary_path)
    return res.get("arch", None)


class QEMU(unittest.TestCase):


    TEST_ARCH_MACHINE_CONSOLES = {
        'alpha': ['clipper'],
        'mips': ['malta'],
        'x86_64': ['isapc',
                   'pc', 'pc-0.10', 'pc-0.11', 'pc-0.12', 'pc-0.13',
                   'pc-0.14', 'pc-0.15', 'pc-1.0', 'pc-1.1', 'pc-1.2',
                   'pc-1.3',
                   'pc-i440fx-1.4', 'pc-i440fx-1.5', 'pc-i440fx-1.6',
                   'pc-i440fx-1.7', 'pc-i440fx-2.0', 'pc-i440fx-2.1',
                   'pc-i440fx-2.10', 'pc-i440fx-2.11', 'pc-i440fx-2.2',
                   'pc-i440fx-2.3', 'pc-i440fx-2.4', 'pc-i440fx-2.5',
                   'pc-i440fx-2.6', 'pc-i440fx-2.7', 'pc-i440fx-2.8',
                   'pc-i440fx-2.9', 'pc-q35-2.10', 'pc-q35-2.11',
                   'q35', 'pc-q35-2.4', 'pc-q35-2.5', 'pc-q35-2.6',
                   'pc-q35-2.7', 'pc-q35-2.8', 'pc-q35-2.9'],
        'ppc64': ['40p', 'powernv', 'prep', 'pseries', 'pseries-2.1',
                  'pseries-2.2', 'pseries-2.3', 'pseries-2.4', 'pseries-2.5',
                  'pseries-2.6', 'pseries-2.7', 'pseries-2.8', 'pseries-2.9',
                  'pseries-2.10', 'pseries-2.11', 'pseries-2.12'],
        's390x': ['s390-ccw-virtio', 's390-ccw-virtio-2.4',
                  's390-ccw-virtio-2.5', 's390-ccw-virtio-2.6',
                  's390-ccw-virtio-2.7', 's390-ccw-virtio-2.8',
                  's390-ccw-virtio-2.9', 's390-ccw-virtio-2.10',
                  's390-ccw-virtio-2.11', 's390-ccw-virtio-2.12']
    }


    def test_set_console(self):
        for machines in QEMU.TEST_ARCH_MACHINE_CONSOLES.values():
            for machine in machines:
                qemu_machine = qemu.QEMUMachine('/fake/path/to/binary')
                qemu_machine.set_machine(machine)
                qemu_machine.set_console()

    def test_set_console_no_machine(self):
        qemu_machine = qemu.QEMUMachine('/fake/path/to/binary')
        self.assertRaises(qemu.QEMUMachineAddDeviceError,
                          qemu_machine.set_console)

    def test_set_console_no_machine_match(self):
        qemu_machine = qemu.QEMUMachine('/fake/path/to/binary')
        qemu_machine.set_machine('unknown-machine-model')
        self.assertRaises(qemu.QEMUMachineAddDeviceError,
                          qemu_machine.set_console)

    @unittest.skipUnless(get_built_qemu_binaries(),
                         "Could not find any QEMU binaries built to use on "
                         "console check")
    def test_set_console_launch(self):
        for binary in get_built_qemu_binaries():
            probed_arch = qemu_bin_probe_arch(binary)
            for machine in QEMU.TEST_ARCH_MACHINE_CONSOLES.get(probed_arch, []):
                qemu_machine = qemu.QEMUMachine(binary)

                # the following workarounds are target specific required for
                # this test.  users are of QEMUMachine are expected to deal with
                # target specific requirements just the same in their own code
                cap_htm_off = ('pseries-2.7', 'pseries-2.8', 'pseries-2.9',
                               'pseries-2.10', 'pseries-2.11', 'pseries-2.12')
                if probed_arch == 'ppc64' and machine in cap_htm_off:
                    qemu_machine._machine = machine   # pylint: disable=W0212
                    qemu_machine.args.extend(['-machine',
                                              '%s,cap-htm=off' % machine])
                elif probed_arch == 's390x':
                    qemu_machine.set_machine(machine)
                    qemu_machine.args.append('-nodefaults')
                elif probed_arch == 'mips':
                    qemu_machine.set_machine(machine)
                    qemu_machine.args.extend(['-bios', os.path.devnull])
                else:
                    qemu_machine.set_machine(machine)

                qemu_machine.set_console()
                qemu_machine.launch()
                qemu_machine.shutdown()
