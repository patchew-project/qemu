#!/usr/bin/env python2.7
#
# Run QEMU with all combinations of -machine and -device types,
# check for crashes and unexpected errors.
#
#  Copyright (c) 2017 Red Hat Inc
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.
#

import sys, os, glob
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'scripts'))

from qtest import QEMUQtestMachine
import logging, traceback, re, random, argparse

logger = logging.getLogger('device-crash-test')
dbg = logger.debug

# Valid whitelist entry keys:
# - accel: regexp, full match only
# - machine: regexp, full match only
# - device: regexp, full match only
# - log: regexp, partial match allowed
# - exitcode: if not present, defaults to 1. If None, matches any exitcode
# - warn: if True, matching failures will be logged as warnings
ERROR_WHITELIST = [
  # Machines that won't work out of the box:
  #             MACHINE          | ERROR MESSAGE
  dict(machine='niagara'),       # Unable to load a firmware for -M niagara
  dict(machine='boston'),        # Please provide either a -kernel or -bios argument
  dict(machine='leon3_generic'), # Can't read bios image (null)

  # devices that don't work out of the box because they require extra options to "-device DEV":
  #            DEVICE                     | ERROR MESSAGE
  dict(device='.*-(i386|x86_64)-cpu'),    # CPU socket-id is not set
  dict(device='ARM,bitband-memory'),      # source-memory property not set
  dict(device='arm.cortex-a9-global-timer'), # a9_gtimer_realize: num-cpu must be between 1 and 4
  dict(device='arm_mptimer'),             # num-cpu must be between 1 and 4
  dict(device='armv7m'),                  # memory property was not set
  dict(device='aspeed.scu'),              # Unknown silicon revision: 0x0
  dict(device='aspeed.sdmc'),             # Unknown silicon revision: 0x0
  dict(device='bcm2835-dma'),             # bcm2835_dma_realize: required dma-mr link not found: Property '.dma-mr' not found
  dict(device='bcm2835-fb'),              # bcm2835_fb_realize: required vcram-base property not set
  dict(device='bcm2835-mbox'),            # bcm2835_mbox_realize: required mbox-mr link not found: Property '.mbox-mr' not found
  dict(device='bcm2835-peripherals'),     # bcm2835_peripherals_realize: required ram link not found: Property '.ram' not found
  dict(device='bcm2835-property'),        # bcm2835_property_realize: required fb link not found: Property '.fb' not found
  dict(device='bcm2835_gpio'),            # bcm2835_gpio_realize: required sdhci link not found: Property '.sdbus-sdhci' not found
  dict(device='bcm2836'),                 # bcm2836_realize: required ram link not found: Property '.ram' not found
  dict(device='cfi.pflash01'),            # attribute "sector-length" not specified or zero.
  dict(device='cfi.pflash02'),            # attribute "sector-length" not specified or zero.
  dict(device='icp'),                     # icp_realize: required link 'xics' not found: Property '.xics' not found
  dict(device='ics'),                     # ics_base_realize: required link 'xics' not found: Property '.xics' not found
  dict(device='ide-drive'),               # No drive specified
  dict(device='ide-hd'),                  # No drive specified
  dict(device='ipmi-bmc-extern'),         # IPMI external bmc requires chardev attribute
  dict(device='isa-debugcon'),            # Can't create serial device, empty char device
  dict(device='isa-ipmi-bt'),             # IPMI device requires a bmc attribute to be set
  dict(device='isa-ipmi-kcs'),            # IPMI device requires a bmc attribute to be set
  dict(device='isa-parallel'),            # Can't create serial device, empty char device
  dict(device='isa-serial'),              # Can't create serial device, empty char device
  dict(device='ivshmem'),                 # You must specify either 'shm' or 'chardev'
  dict(device='ivshmem-doorbell'),        # You must specify a 'chardev'
  dict(device='ivshmem-plain'),           # You must specify a 'memdev'
  dict(device='kvm-pci-assign'),          # no host device specified
  dict(device='loader'),                  # please include valid arguments
  dict(device='nand'),                    #' Unsupported NAND block size 0x1
  dict(device='nvdimm'),                  # 'memdev' property is not set
  dict(device='nvme'),                    # Device initialization failed
  dict(device='pc-dimm'),                 # 'memdev' property is not set
  dict(device='pci-bridge'),              # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='pci-bridge-seat'),         # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='pci-serial'),              # Can't create serial device, empty char device
  dict(device='pci-serial-2x'),           # Can't create serial device, empty char device
  dict(device='pci-serial-4x'),           # Can't create serial device, empty char device
  dict(device='pxa2xx-dma'),              # channels value invalid
  dict(device='pxb'),                     # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='scsi-block'),              # drive property not set
  dict(device='scsi-disk'),               # drive property not set
  dict(device='scsi-generic'),            # drive property not set
  dict(device='scsi-hd'),                 # drive property not set
  dict(device='spapr-pci-host-bridge'),   # BUID not specified for PHB
  dict(device='spapr-pci-vfio-host-bridge'), # BUID not specified for PHB
  dict(device='spapr-rng'),               #' spapr-rng needs an RNG backend!
  dict(device='spapr-vty'),               # chardev property not set
  dict(device='tpm-tis'),                 # tpm_tis: backend driver with id (null) could not be found
  dict(device='unimplemented-device'),    # property 'size' not specified or zero
  dict(device='usb-braille'),             # Property chardev is required
  dict(device='usb-mtp'),                 # x-root property must be configured
  dict(device='usb-redir'),               # Parameter 'chardev' is missing
  dict(device='usb-serial'),              # Property chardev is required
  dict(device='usb-storage'),             # drive property not set
  dict(device='vfio-amd-xgbe'),           # -device vfio-amd-xgbe: vfio error: wrong host device name
  dict(device='vfio-calxeda-xgmac'),      # -device vfio-calxeda-xgmac: vfio error: wrong host device name
  dict(device='vfio-pci'),                # No provided host device
  dict(device='vfio-pci-igd-lpc-bridge'), # VFIO dummy ISA/LPC bridge must have address 1f.0
  dict(device='vhost-scsi.*'),            # vhost-scsi: missing wwpn
  dict(device='vhost-vsock-device'),      # guest-cid property must be greater than 2
  dict(device='vhost-vsock-pci'),         # guest-cid property must be greater than 2
  dict(device='virtio-9p-ccw'),           # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-9p-device'),        # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-9p-pci'),           # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-blk-ccw'),          # drive property not set
  dict(device='virtio-blk-device'),       # drive property not set
  dict(device='virtio-blk-device'),       # drive property not set
  dict(device='virtio-blk-pci'),          # drive property not set
  dict(device='virtio-crypto-ccw'),       # 'cryptodev' parameter expects a valid object
  dict(device='virtio-crypto-device'),    # 'cryptodev' parameter expects a valid object
  dict(device='virtio-crypto-pci'),       # 'cryptodev' parameter expects a valid object
  dict(device='virtio-input-host-device'), # evdev property is required
  dict(device='virtio-input-host-pci'),   # evdev property is required
  dict(device='xen-pvdevice'),            # Device ID invalid, it must always be supplied
  dict(device='vhost-vsock-ccw'),         # guest-cid property must be greater than 2
  dict(device='ALTR.timer'),              # "clock-frequency" property must be provided
  dict(device='zpci'),                    # target must be defined

  # ioapic devices are already created by pc and will fail:
  dict(machine='pc-.*', device='kvm-ioapic'), # Only 1 ioapics allowed
  dict(machine='pc-.*', device='ioapic'),     # Only 1 ioapics allowed

  # KVM-specific devices shouldn't be tried without accel=kvm:
  dict(accel='(?!kvm).*', device='kvmclock'),
  dict(accel='(?!kvm).*', device='kvm-pci-assign'),

  # xen-specific machines and devices:
  dict(accel='(?!xen).*', machine='xenfv'), # xenfv machine requires the xen accelerator
  dict(accel='(?!xen).*', machine='xenpv'),
  dict(accel='(?!xen).*', device='xen-platform'), # xen-platform device requires the Xen accelerator
  dict(device='xen-pci-passthrough'),  # Could not open '/sys/bus/pci/devices/0000:00:00.0/config': Permission denied

  # Some error messages that are common on multiple devices/machines:
  dict(log=r"No '[\w-]+' bus found for device '[\w-]+'"),
  dict(log=r"images* must be given with the 'pflash' parameter"),
  dict(log=r"(Guest|ROM|Flash|Kernel) image must be specified"),
  dict(log=r"[cC]ould not load [\w ]+ (BIOS|bios) '[\w-]+\.bin'"),
  dict(log=r"Couldn't find rom image '[\w-]+\.bin'"),
  dict(log=r"speed mismatch trying to attach usb device"),
  dict(log=r"Can't create a second ISA bus"),
  dict(log=r"duplicate fw_cfg file name"),
  # sysbus-related error messages: most machines reject most dynamic sysbus devices:
  dict(log=r"Option '-device [\w.,-]+' cannot be handled by this machine"),
  dict(log=r"Device [\w.,-]+ is not supported by this machine yet"),
  dict(log=r"Device [\w.,-]+ can not be dynamically instantiated"),
  dict(log=r"Platform Bus: Can not fit MMIO region of size "),
  # other more specific errors we will ignore:
  dict(device='allwinner-a10', log="Unsupported NIC model:"),
  dict(device='.*-spapr-cpu-core', log=r"CPU core type should be"),
  dict(log=r"MSI(-X)? is not supported by interrupt controller"),
  dict(log=r"pxb-pcie? devices cannot reside on a PCIe? bus"),
  dict(log=r"Ignoring smp_cpus value"),
  dict(log=r"sd_init failed: Drive 'sd0' is already in use because it has been automatically connected to another device"),
  dict(log=r"This CPU requires a smaller page size than the system is using"),
  dict(log=r"MSI-X support is mandatory in the S390 architecture"),
  dict(log=r"rom check and register reset failed"),
  dict(log=r"Unable to initialize GIC, CPUState for CPU#0 not valid"),
  dict(log=r"Multiple VT220 operator consoles are not supported"),
  dict(log=r"core 0 already populated"),

  # KNOWN CRASHES:
  # known crashes will generate error messages, but won't be fatal:
  dict(exitcode=-6, log=r"Device 'serial0' is in use", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"spapr_rtas_register: Assertion .*rtas_table\[token\]\.name.* failed", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"qemu_net_client_setup: Assertion `!peer->peer' failed", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r'RAMBlock "[\w.-]+" already registered', loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"find_ram_offset: Assertion `size != 0' failed.", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"puv3_load_kernel: Assertion `kernel_filename != NULL' failed", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"add_cpreg_to_hashtable: code should not be reached", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"qemu_alloc_display: Assertion `surface->image != NULL' failed", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"Unexpected error in error_set_from_qdev_prop_error", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"Object .* is not an instance of type spapr-machine", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"Object .* is not an instance of type generic-pc-machine", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"Object .* is not an instance of type e500-ccsr", loglevel=logging.ERROR),
  dict(exitcode=-6, log=r"vmstate_register_with_alias_id: Assertion `!se->compat || se->instance_id == 0' failed", loglevel=logging.ERROR),
  dict(exitcode=-11, device='stm32f205-soc', loglevel=logging.ERROR),
  dict(exitcode=-11, device='xlnx,zynqmp', loglevel=logging.ERROR),
  dict(exitcode=-11, device='mips-cps', loglevel=logging.ERROR),
  dict(exitcode=-11, device='gus', loglevel=logging.ERROR),
  dict(exitcode=-11, device='a9mpcore_priv', loglevel=logging.ERROR),
  dict(exitcode=-11, device='isa-serial', loglevel=logging.ERROR),
  dict(exitcode=-11, machine='isapc', device='.*-iommu', loglevel=logging.ERROR),

  # other exitcode=1 failures not listed above will generate warnings:
  dict(exitcode=1, loglevel=logging.WARN),

  # everything else (including SIGABRT and SIGSEGV) will be a fatal error:
  dict(exitcode=None, fatal=True, loglevel=logging.FATAL),
]

def checkWhitelist(f):
    """Look up whitelist entry for failure dictionary

    Returns index in ERROR_WHITELIST
    """
    t = f['testcase']
    for i,wl in enumerate(ERROR_WHITELIST):
        #dbg("whitelist entry: %r", wl)
        if (wl.get('exitcode', 1) is None or
            f['exitcode'] == wl.get('exitcode', 1)) \
           and (not wl.has_key('machine') \
                or re.match(wl['machine'] +'$', t['machine'])) \
           and (not wl.has_key('accel') \
                or re.match(wl['accel'] +'$', t['accel'])) \
           and (not wl.has_key('device') \
                or re.match(wl['device'] +'$', t['device'])) \
           and (not wl.has_key('log') \
                or re.search(wl['log'], f['log'], re.MULTILINE)):
            return i, wl

    raise Exception("this should never happen")

def qemuOptsEscape(s):
    return s.replace(",", ",,")

def formatTestCase(t):
    return ' '.join('%s=%s' % (k, v) for k,v in t.items())

def qomListTypeNames(vm, **kwargs):
    """Run qom-list-types QMP command, return type names"""
    types = vm.command('qom-list-types', **kwargs)
    return [t['name'] for t in types]

def infoQDM(vm):
    """Parse 'info qdm' output"""
    args = {'command-line': 'info qdm'}
    devhelp = vm.command('human-monitor-command', **args)
    for l in devhelp.split('\n'):
        l = l.strip()
        if l == '' or l.endswith(':'):
            continue
        d = {'name': re.search(r'name "([^"]+)"', l).group(1),
             'no-user': (re.search(', no-user', l) is not None)}
        #dbg('info qdm item: %r', d)
        yield d


class QemuBinaryInfo:
    def __init__(self, binary):
        args = ['-S', '-machine', 'none,accel=kvm:tcg']
        dbg("querying info for QEMU binary: %s", binary)
        vm = QEMUQtestMachine(binary=binary, args=args)
        vm.launch()
        try:

            self.alldevs = set(qomListTypeNames(vm, implements='device', abstract=False))
            # there's no way to query cannot_instantiate_with_device_add_yet using QMP,
            # so use 'info qdm':
            self.no_user_devs = set([d['name'] for d in infoQDM(vm, ) if d['no-user']])
            self.machines = list(m['name'] for m in vm.command('query-machines'))
            self.user_devs = self.alldevs.difference(self.no_user_devs)
            self.kvm_available = vm.command('query-kvm')['enabled']
        finally:
            vm.shutdown()

BINARY_INFO = {}
def getBinaryInfo(binary):
    if not BINARY_INFO.has_key(binary):
        BINARY_INFO[binary] = QemuBinaryInfo(binary)
    return BINARY_INFO[binary]

def checkOneCase(testcase):
    """Check one specific case

    Returns a dictionary containing failure information on error,
    or None on success
    """
    binary = testcase['binary']
    accel = testcase['accel']
    machine = testcase['machine']
    device = testcase['device']
    info = getBinaryInfo(binary)

    dbg("will test: %r", testcase)

    args = ['-S', '-machine', '%s,accel=%s' % (machine, accel),
            '-device', qemuOptsEscape(device)]
    cmdline = ' '.join([binary] + args)
    dbg("will launch QEMU: %s", cmdline)
    vm = QEMUQtestMachine(binary=binary, args=args)

    exception = None
    try:
        vm.launch()
        logger.info("success: %s", cmdline)
    except KeyboardInterrupt:
        raise
    except Exception,e:
        exception = e
    finally:
        vm.shutdown()
        ec = vm.exitcode()
        log = vm.get_log()

    if exception is not None or ec != 0:
        f = dict(exception = e,
                 exitcode = ec,
                 log = log,
                 testcase = testcase,
                 cmdline=cmdline)
        return f

def binariesToTest(args, testcase):
    if args.qemu:
        r = args.qemu
    else:
        r = glob.glob('./*-softmmu/qemu-system-*')
    return r

def accelsToTest(args, testcase):
    if getBinaryInfo(testcase['binary']).kvm_available:
        yield 'kvm'
    yield 'tcg'

def machinesToTest(args, testcase):
    return getBinaryInfo(testcase['binary']).machines

def devicesToTest(args, testcase):
    return getBinaryInfo(testcase['binary']).user_devs

TESTCASE_VARIABLES = [
    ('binary', binariesToTest),
    ('accel', accelsToTest),
    ('machine', machinesToTest),
    ('device', devicesToTest),
]

def genCases1(args, testcases, var, fn):
    """Generate new testcases for one variable

    If an existing item already has a variable set, don't
    generate new items and just return it directly. This
    allows the "-t" command-line option to be used to choose
    a specific test case.
    """
    for testcase in testcases:
        t = testcase.copy
        if testcase.has_key(var):
            yield testcase.copy()
        else:
            for i in fn(args, testcase):
                t = testcase.copy()
                t[var] = i
                yield t

def genCases(args, testcase):
    """Generate test cases for all variables

    If args.random is set, generate a single item. Otherwise,
    return all possible cases.
    """
    cases = [testcase.copy()]
    for var,fn in TESTCASE_VARIABLES:
        dbg("var: %r, fn: %r", var, fn)
        cases = genCases1(args, cases, var, fn)
        if args.random:
            cases = list(cases)
            dbg("cases: %r", cases)
            cases = [random.choice(cases)]
            dbg("case: %r", cases)
    return cases

def genAllCases(args, testcase):
    return genCases(args, testcase)

def pickRandomCase(args, testcase):
    cases = list(genCases(args, testcase))
    assert len(cases) == 1
    return cases[0]

def casesToTest(args, testcase):
    if args.random:
        cases = [pickRandomCase(args, testcase) for i in xrange(args.random)]
    else:
        cases = genAllCases(args, testcase)
    if args.debug:
        cases = list(cases)
        dbg("%d test cases to test", len(cases))
    if args.shuffle:
        cases = list(cases)
        random.shuffle(cases)
    return cases

def logFailure(f, level):
    t = f['testcase']
    logger.log(level, "failed: %s", formatTestCase(t))
    logger.log(level, "cmdline: %s", f['cmdline'])
    for l in f['log'].strip().split('\n'):
        logger.log(level, "log: %s", l)
    logger.log(level, "exit code: %r", f['exitcode'])

def main():
    parser = argparse.ArgumentParser(description="QEMU -device crash test")
    parser.add_argument('-t', metavar='KEY=VALUE', nargs='*',
                        help="Limit test cases to KEY=VALUE",
                        dest='testcases')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug output')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='verbose output')
    parser.add_argument('-r', '--random', type=int, metavar='COUNT',
                        help='run a random sample of COUNT test cases',
                        default=0)
    parser.add_argument('--shuffle', action='store_true',
                        help='Run test cases in random order')
    parser.add_argument('--dry-run', action='store_true',
                        help="Don't run any tests, just generate list")
    parser.add_argument('qemu', nargs='*', metavar='QEMU',
                        help='QEMU binary to run')
    args = parser.parse_args()

    if args.debug:
        lvl = logging.DEBUG
    elif args.verbose:
        lvl = level=logging.INFO
    else:
        lvl = level=logging.WARN
    logging.basicConfig(level=lvl, format='%(levelname)s: %(message)s')


    interrupted = False
    fatal_failures = []
    wl_stats = {}

    tc = {}
    if args.testcases:
        for t in args.testcases:
            for kv in t.split():
                k,v = kv.split('=', 1)
                tc[k] = v

    if len(binariesToTest(args, tc)) == 0:
        print >>sys.stderr, "No QEMU binary found"
        parser.print_usage(sys.stderr)
        return 1

    for t in casesToTest(args, tc):
        logger.info("test case: %s", formatTestCase(t))
        if args.dry_run:
            continue

        try:
            f = checkOneCase(t)
        except KeyboardInterrupt:
            interrupted = True
            break
        if f:
            i,wl = checkWhitelist(f)
            dbg("testcase: %r, whitelist match: %r", t, wl)
            wl_stats.setdefault(i, []).append(f)
            logFailure(f, wl.get('loglevel', logging.DEBUG))
            if wl.get('fatal'):
                fatal_failures.append(f)

    # tell us about obsolete whitelist entries so we can clean it up after
    # bugs are fixed:
    if not interrupted and not args.random and not args.dry_run:
        for i,wl in enumerate(ERROR_WHITELIST):
            if not wl_stats.get(i):
                logger.info("Obsolete whitelist entry? %r", wl)

    stats = sorted([(len(wl_stats[i]), i) for i in wl_stats])
    for count,i in stats:
        dbg("whitelist entry stats: %d: %r", count, ERROR_WHITELIST[i])

    if fatal_failures:
        for f in fatal_failures:
            t = f['testcase']
            logger.error("Fatal failure: %s", formatTestCase(t))
        logger.error("Fatal failures on some machine/device combinations")
        return 1

if __name__ == '__main__':
    sys.exit(main())
