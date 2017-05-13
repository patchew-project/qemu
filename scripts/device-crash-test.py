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

from itertools import chain
from qemu import QEMUMachine
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
# - expected: if True, QEMU is expected to always fail every time
#   when testing the corresponding test case
ERROR_WHITELIST = [
  # Machines that won't work out of the box:
  #             MACHINE                         | ERROR MESSAGE
  dict(machine='niagara', expected=True),       # Unable to load a firmware for -M niagara
  dict(machine='boston', expected=True),        # Please provide either a -kernel or -bios argument
  dict(machine='leon3_generic', expected=True), # Can't read bios image (null)

  # devices that don't work out of the box because they require extra options to "-device DEV":
  #            DEVICE                                    | ERROR MESSAGE
  dict(device='.*-(i386|x86_64)-cpu', expected=True),    # CPU socket-id is not set
  dict(device='ARM,bitband-memory', expected=True),      # source-memory property not set
  dict(device='arm.cortex-a9-global-timer', expected=True), # a9_gtimer_realize: num-cpu must be between 1 and 4
  dict(device='arm_mptimer', expected=True),             # num-cpu must be between 1 and 4
  dict(device='armv7m', expected=True),                  # memory property was not set
  dict(device='aspeed.scu', expected=True),              # Unknown silicon revision: 0x0
  dict(device='aspeed.sdmc', expected=True),             # Unknown silicon revision: 0x0
  dict(device='bcm2835-dma', expected=True),             # bcm2835_dma_realize: required dma-mr link not found: Property '.dma-mr' not found
  dict(device='bcm2835-fb', expected=True),              # bcm2835_fb_realize: required vcram-base property not set
  dict(device='bcm2835-mbox', expected=True),            # bcm2835_mbox_realize: required mbox-mr link not found: Property '.mbox-mr' not found
  dict(device='bcm2835-peripherals', expected=True),     # bcm2835_peripherals_realize: required ram link not found: Property '.ram' not found
  dict(device='bcm2835-property', expected=True),        # bcm2835_property_realize: required fb link not found: Property '.fb' not found
  dict(device='bcm2835_gpio', expected=True),            # bcm2835_gpio_realize: required sdhci link not found: Property '.sdbus-sdhci' not found
  dict(device='bcm2836', expected=True),                 # bcm2836_realize: required ram link not found: Property '.ram' not found
  dict(device='cfi.pflash01', expected=True),            # attribute "sector-length" not specified or zero.
  dict(device='cfi.pflash02', expected=True),            # attribute "sector-length" not specified or zero.
  dict(device='icp', expected=True),                     # icp_realize: required link 'xics' not found: Property '.xics' not found
  dict(device='ics', expected=True),                     # ics_base_realize: required link 'xics' not found: Property '.xics' not found
  dict(device='ide-drive', expected=True),               # No drive specified
  dict(device='ide-hd', expected=True),                  # No drive specified
  dict(device='ipmi-bmc-extern', expected=True),         # IPMI external bmc requires chardev attribute
  dict(device='isa-debugcon', expected=True),            # Can't create serial device, empty char device
  dict(device='isa-ipmi-bt', expected=True),             # IPMI device requires a bmc attribute to be set
  dict(device='isa-ipmi-kcs', expected=True),            # IPMI device requires a bmc attribute to be set
  dict(device='isa-parallel', expected=True),            # Can't create serial device, empty char device
  dict(device='isa-serial', expected=True),              # Can't create serial device, empty char device
  dict(device='ivshmem', expected=True),                 # You must specify either 'shm' or 'chardev'
  dict(device='ivshmem-doorbell', expected=True),        # You must specify a 'chardev'
  dict(device='ivshmem-plain', expected=True),           # You must specify a 'memdev'
  dict(device='kvm-pci-assign', expected=True),          # no host device specified
  dict(device='loader', expected=True),                  # please include valid arguments
  dict(device='nand', expected=True),                    #' Unsupported NAND block size 0x1
  dict(device='nvdimm', expected=True),                  # 'memdev' property is not set
  dict(device='nvme', expected=True),                    # Device initialization failed
  dict(device='pc-dimm', expected=True),                 # 'memdev' property is not set
  dict(device='pci-bridge', expected=True),              # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='pci-bridge-seat', expected=True),         # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='pci-serial', expected=True),              # Can't create serial device, empty char device
  dict(device='pci-serial-2x', expected=True),           # Can't create serial device, empty char device
  dict(device='pci-serial-4x', expected=True),           # Can't create serial device, empty char device
  dict(device='pxa2xx-dma', expected=True),              # channels value invalid
  dict(device='pxb', expected=True),                     # Bridge chassis not specified. Each bridge is required to be assigned a unique chassis id > 0.
  dict(device='scsi-block', expected=True),              # drive property not set
  dict(device='scsi-disk', expected=True),               # drive property not set
  dict(device='scsi-generic', expected=True),            # drive property not set
  dict(device='scsi-hd', expected=True),                 # drive property not set
  dict(device='spapr-pci-host-bridge', expected=True),   # BUID not specified for PHB
  dict(device='spapr-pci-vfio-host-bridge', expected=True), # BUID not specified for PHB
  dict(device='spapr-rng', expected=True),               #' spapr-rng needs an RNG backend!
  dict(device='spapr-vty', expected=True),               # chardev property not set
  dict(device='tpm-tis', expected=True),                 # tpm_tis: backend driver with id (null) could not be found
  dict(device='unimplemented-device', expected=True),    # property 'size' not specified or zero
  dict(device='usb-braille', expected=True),             # Property chardev is required
  dict(device='usb-mtp', expected=True),                 # x-root property must be configured
  dict(device='usb-redir', expected=True),               # Parameter 'chardev' is missing
  dict(device='usb-serial', expected=True),              # Property chardev is required
  dict(device='usb-storage', expected=True),             # drive property not set
  dict(device='vfio-amd-xgbe', expected=True),           # -device vfio-amd-xgbe: vfio error: wrong host device name
  dict(device='vfio-calxeda-xgmac', expected=True),      # -device vfio-calxeda-xgmac: vfio error: wrong host device name
  dict(device='vfio-pci', expected=True),                # No provided host device
  dict(device='vfio-pci-igd-lpc-bridge', expected=True), # VFIO dummy ISA/LPC bridge must have address 1f.0
  dict(device='vhost-scsi.*', expected=True),            # vhost-scsi: missing wwpn
  dict(device='vhost-vsock-device', expected=True),      # guest-cid property must be greater than 2
  dict(device='vhost-vsock-pci', expected=True),         # guest-cid property must be greater than 2
  dict(device='virtio-9p-ccw', expected=True),           # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-9p-device', expected=True),        # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-9p-pci', expected=True),           # 9pfs device couldn't find fsdev with the id = NULL
  dict(device='virtio-blk-ccw', expected=True),          # drive property not set
  dict(device='virtio-blk-device', expected=True),       # drive property not set
  dict(device='virtio-blk-device', expected=True),       # drive property not set
  dict(device='virtio-blk-pci', expected=True),          # drive property not set
  dict(device='virtio-crypto-ccw', expected=True),       # 'cryptodev' parameter expects a valid object
  dict(device='virtio-crypto-device', expected=True),    # 'cryptodev' parameter expects a valid object
  dict(device='virtio-crypto-pci', expected=True),       # 'cryptodev' parameter expects a valid object
  dict(device='virtio-input-host-device', expected=True), # evdev property is required
  dict(device='virtio-input-host-pci', expected=True),   # evdev property is required
  dict(device='xen-pvdevice', expected=True),            # Device ID invalid, it must always be supplied
  dict(device='vhost-vsock-ccw', expected=True),         # guest-cid property must be greater than 2
  dict(device='ALTR.timer', expected=True),              # "clock-frequency" property must be provided
  dict(device='zpci', expected=True),                    # target must be defined

  # ioapic devices are already created by pc and will fail:
  dict(machine='q35|pc.*', device='kvm-ioapic', expected=True), # Only 1 ioapics allowed
  dict(machine='q35|pc.*', device='ioapic', expected=True),     # Only 1 ioapics allowed

  # KVM-specific devices shouldn't be tried without accel=kvm:
  dict(accel='(?!kvm).*', device='kvmclock', expected=True),
  dict(accel='(?!kvm).*', device='kvm-pci-assign', expected=True),

  # xen-specific machines and devices:
  dict(accel='(?!xen).*', machine='xen.*', expected=True),
  dict(accel='(?!xen).*', device='xen-.*', expected=True),

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
  dict(exitcode=-11, device='stm32f205-soc', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='xlnx,zynqmp', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='mips-cps', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='gus', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='a9mpcore_priv', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='a15mpcore_priv', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='isa-serial', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='sb16', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='cs4231a', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, device='arm-gicv3', loglevel=logging.ERROR, expected=True),
  dict(exitcode=-11, machine='isapc', device='.*-iommu', loglevel=logging.ERROR, expected=True),

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
  #TODO: expected=True entries for unsupported sysbus devices
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
  dict(log=r"could not find stage1 bootloader"),

  # other exitcode=1 failures not listed above will generate warnings:
  dict(exitcode=1, loglevel=logging.WARN),

  # everything else (including SIGABRT and SIGSEGV) will be a fatal error:
  dict(exitcode=None, fatal=True, loglevel=logging.FATAL),
]

def whitelistMatch(wl, f):
    t = f.get('testcase', {})
    return (not wl.has_key('machine') \
            or not t.has_key('machine') \
            or re.match(wl['machine'] +'$', t['machine'])) \
           and (not wl.has_key('accel') \
                or not t.has_key('accel') \
                or re.match(wl['accel'] +'$', t['accel'])) \
           and (not wl.has_key('device') \
                or not t.has_key('device') \
                or re.match(wl['device'] +'$', t['device'])) \
           and (wl.get('exitcode', 1) is None \
                or not f.has_key('exitcode')
                or f['exitcode'] == wl.get('exitcode', 1)) \
           and (not wl.has_key('log') \
                or not f.has_key('log') \
                or re.search(wl['log'], f['log'], re.MULTILINE))

def checkWhitelist(f):
    """Look up whitelist entry for failure dictionary

    Returns index in ERROR_WHITELIST
    """
    for i,wl in enumerate(ERROR_WHITELIST):
        #dbg("whitelist entry: %r", wl)
        if whitelistMatch(wl, f):
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
    def __init__(self, binary, devtype):
        if devtype is None:
            devtype = 'device'

        dbg("devtype: %r", devtype)
        args = ['-S', '-machine', 'none,accel=kvm:tcg']
        dbg("querying info for QEMU binary: %s", binary)
        vm = QEMUMachine(binary=binary, args=args)
        vm.launch()
        try:
            self.alldevs = set(qomListTypeNames(vm, implements=devtype, abstract=False))
            # there's no way to query cannot_instantiate_with_device_add_yet using QMP,
            # so use 'info qdm':
            self.no_user_devs = set([d['name'] for d in infoQDM(vm, ) if d['no-user']])
            self.machines = list(m['name'] for m in vm.command('query-machines'))
            self.user_devs = self.alldevs.difference(self.no_user_devs)
            self.kvm_available = vm.command('query-kvm')['enabled']
        finally:
            vm.shutdown()

BINARY_INFO = {}
def getBinaryInfo(args, binary):
    if not BINARY_INFO.has_key(binary):
        BINARY_INFO[binary] = QemuBinaryInfo(binary, args.devtype)
    return BINARY_INFO[binary]

def checkOneCase(args, testcase):
    """Check one specific case

    Returns a dictionary containing failure information on error,
    or None on success
    """
    binary = testcase['binary']
    accel = testcase['accel']
    machine = testcase['machine']
    device = testcase['device']
    info = getBinaryInfo(args, binary)

    dbg("will test: %r", testcase)

    args = ['-S', '-machine', '%s,accel=%s' % (machine, accel),
            '-device', qemuOptsEscape(device)]
    cmdline = ' '.join([binary] + args)
    dbg("will launch QEMU: %s", cmdline)
    vm = QEMUMachine(binary=binary, args=args)

    exception = None
    try:
        vm.launch()
    except KeyboardInterrupt:
        raise
    except Exception,e:
        exception = e
    finally:
        vm.shutdown()
        ec = vm.exitcode()
        log = vm.get_log()

    if exception is not None or ec != 0:
        f = dict(exception = exception,
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
    if getBinaryInfo(args, testcase['binary']).kvm_available:
        yield 'kvm'
    yield 'tcg'

def machinesToTest(args, testcase):
    return getBinaryInfo(args, testcase['binary']).machines

def devicesToTest(args, testcase):
    return getBinaryInfo(args, testcase['binary']).user_devs

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
    """
    cases = [testcase.copy()]
    for var,fn in TESTCASE_VARIABLES:
        dbg("var: %r, fn: %r", var, fn)
        cases = genCases1(args, cases, var, fn)
    return cases

def genAllCases(args, testcase):
    return genCases(args, testcase)

def pickRandomCase(args, testcase):
    cases = list(genCases(args, testcase))
    if cases:
        assert len(cases) == 1
        return cases[0]

def casesToTest(args, testcase):
    cases = genAllCases(args, testcase)
    if args.random:
        cases = random.sample(list(cases), args.random)
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
                        action='append', dest='testcases', default=[])
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
    parser.add_argument('-D', '--devtype', metavar='TYPE',
                        help="Test only device types that implement TYPE")
    parser.add_argument('-Q', '--quick', action='store_true',
                        help="Quick mode, skip test cases that are expected to fail")
    parser.add_argument('qemu', nargs='*', metavar='QEMU',
                        help='QEMU binary to run')
    args = parser.parse_args()

    if args.debug:
        lvl = logging.DEBUG
    elif args.verbose:
        lvl = level=logging.INFO
    else:
        lvl = level=logging.WARN
    logging.basicConfig(stream=sys.stdout, level=lvl, format='%(levelname)s: %(message)s')


    interrupted = False
    fatal_failures = []
    wl_stats = {}
    skipped = 0
    total = 0

    tc = {}
    dbg("testcases: %r", args.testcases)
    if args.testcases:
        for t in chain(*args.testcases):
            for kv in t.split():
                k,v = kv.split('=', 1)
                tc[k] = v

    if len(binariesToTest(args, tc)) == 0:
        print >>sys.stderr, "No QEMU binary found"
        parser.print_usage(sys.stderr)
        return 1

    for t in casesToTest(args, tc):
        logger.info("running test case: %s", formatTestCase(t))
        total += 1

        _,expected_match = checkWhitelist({'testcase': t})
        if args.quick and expected_match.get('expected'):
            dbg("Skipped: %s", formatTestCase(t))
            skipped += 1
            continue

        if args.dry_run:
            continue

        try:
            f = checkOneCase(args, t)
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
        else:
            dbg("success: %s", formatTestCase(t))
            if expected_match.get('expected'):
                logger.warn("Didn't fail as expected: %s", formatTestCase(t))

    logger.info("Total: %d test cases", total)
    if skipped:
        logger.info("Skipped %d test cases", skipped)

    # tell us about obsolete whitelist entries so we can clean it up after
    # bugs are fixed:
    if not (interrupted or args.random or args.dry_run \
            or args.testcases or args.qemu or args.devtype):
        for i,wl in enumerate(ERROR_WHITELIST):
            if not wl_stats.get(i) and not wl.get('fatal'):
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
