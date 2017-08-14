#!/usr/bin/env python
import sys, os
MY_DIR = os.path.dirname(__file__)
sys.path.append(os.path.join(MY_DIR, '..', 'scripts'))
import qtest
import unittest
import logging
import argparse
import itertools
import operator
import re

logger = logging.getLogger('qemu.tests.machineinfo')

# machines that we can't easily test because they can't run on all hosts:
BLACKLIST = set(['xenpv', 'xenfv'])

# machines known to be broken when using -nodefaults:
NODEFAULTS_BLACKLIST = set([
    'cubieboard',      # segfaults
    'petalogix-ml605', # segfaults
    'or32-sim',        # segfaults
    'virtex-ml507',    # segfaults
    'Niagara',         # segfaults
    'akita',           # "qemu: missing SecureDigital device"
    'borzoi',          # "qemu: missing SecureDigital device"
    'cheetah',         # "qemu: missing SecureDigital device"
    'connex',          # "qemu: missing SecureDigital device"
    'mainstone',       # "qemu: missing SecureDigital device"
    'n800',            # "qemu: missing SecureDigital device"
    'n810',            # "qemu: missing SecureDigital device"
    'spitz',           # "qemu: missing SecureDigital device"
    'sx1',             # "qemu: missing SecureDigital device"
    'sx1-v1',          # "qemu: missing SecureDigital device"
    'terrier',         # "qemu: missing SecureDigital device"
    'tosa',            # "qemu: missing SecureDigital device"
    'verdex',          # "qemu: missing SecureDigital device"
    'z2',              # "qemu: missing SecureDigital device"
])

# iterators for QAPI ValueSets:
# all of the iterators below should support iter() and len()

class InvalidValueSet(Exception):
    pass

class ElementIterator:
    def __init__(self, e):
        self._e = e

    def _data(self):
        e = self._e
        if type(e) is not list:
            e = [e]
        if len(e) == 1:
            return e
        elif len(e) == 2:
            return xrange(e[0], e[1] + 1)
        else:
            raise InvalidValueSet

    def __iter__(self):
        return iter(self._data())

    def __len__(self):
        return len(self._data())

class ValuesIterator:
    def __init__(self, values):
        if type(values) is not list:
            values = [values]
        self._values = values

    def _iterators(self):
        return map(ElementIterator, self._values)

    def __iter__(self):
        return itertools.chain(self._iterators())

    def __len__(self):
        return sum(map(len, self._iterators()))


#TODO: move to common code
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
        yield d



class QueryMachinesTest(unittest.TestCase):
    def setUp(self):
        self.vm = None

    def tearDown(self):
        if self.vm is not None:
            self.vm.shutdown()

    def walkQOMTree(self, vm, path):
        """Walk QOM tree recusrively, starting at path"""
        children = vm.qmp('qom-list', path=path)['return']
        for c in children:
            logging.debug('walking %s. child: %s', path, c)
            if not c['type'].startswith('child<'):
                continue

            cp = '%s/%s' % (path, c['name'])
            yield cp

            for gc in self.walkQOMTree(vm, cp):
                yield gc

    def findAllBuses(self, vm):
        """Find all bus objects in the QOM tree"""
        r = vm.qmp('qom-list-types', implements='bus')
        bus_types = set([b['name'] for b in r['return']])
        for cp in self.walkQOMTree(vm, '/machine'):
            t = vm.qmp('qom-get', path=cp, property='type')['return']
            if t in bus_types:
                yield dict(path=cp, type=t)

    def getSubtypes(self, implements, **kwargs):
        """Get full list of typenames of subtypes of @implements"""
        types = self.vm.command('qom-list-types', implements=implements, **kwargs)
        return [st['name'] for st in types]


    def getUserCreatableSubtypes(self, devtype):
        alldevs = set(self.getSubtypes(devtype, abstract=False))
        # there's no way to query DeviceClass::user_creatable using QMP,
        # so use 'info qdm':
        no_user_devs = set([d['name'] for d in infoQDM(self.vm, ) if d['no-user']])
        user_devs = alldevs.difference(no_user_devs)
        return user_devs

    def typeImplements(self, t, i):
        """Returns True if type @t implements type @i"""
        stypes = self.getSubtypes(i)
        return t in stypes

    def validateBus(self, bus, slots):
        """Check if the bus identified by the slot matches the information returned
        for the slot.

        TODO: check if it is really a bus
        TODO: check if device-types matches device-types
              property in the bus
        """

        ##we could do this:
        #bustype = self.vm.command('qom-get', path=bus, property='type')
        #self.assertTrue(self.typeImplements(bustype, 'bus'))
        ## but the bus _name_ (accepted by qbus_find()) does not necessarily matches the bus _path_

        pass

    def checkSlotProps(self, slots):
        """check if all properties on "props" are valid properties
        that appear on device-list-properties for all accepted device types
        """
        types_to_check = {}
        buses_to_check = {}
        for slot in slots:
            for prop in slot['opts']:
                if prop['option'] == 'bus':
                    values = ValuesIterator(bus['values'])
                    self.assertEquals(len(values), 1)
                    bus = values[0]
                    buses_to_check.setdefault(v, []).append(slot)

            for t in slot['device-types']:
                types_to_check.setdefault(t, set()).update(slot['opts'].keys())

        for bus,slots in buses_to_check.items():
            self.validateBus(bus, slots)

        for t, props in types_to_check.items():
            props.discard('bus') # 'bus' is handled by device_add directly
            for st in self.vm.command('qom-list-types', implements=t, abstract=False):
                dprops = self.vm.command('device-list-properties', typename=st['name'])
                dpropnames = set([p['name'] for p in dprops])
                for p in props:
                    self.assertIn(p, dpropnames)

    def checkAvailableField(self, slots):
        for slot in slots:
            if slot.has_key('max-devices') and len(slot['devices']) >= slot['max-devices']:
                self.assertFalse(slot['available'])

    def checkSlotInfo(self, args):
        #TODO: check if:
        # * -device works with at least one device type
        # * query-hotpluggable-cpus matches what's in query-device-slots
        # * device-types match the property on the bus
        # * available=false if hotpluggable=false
        # * 'count' is always set if not incomplete
        # * slot count is <= set of possible values for @props
        self.vm = qtest.QEMUQtestMachine(self.binary, args=args)
        self.vm.launch()

        slots = self.vm.command('query-device-slots')
        #self.checkSlotProps(slots)
        #self.checkSlotDevices(slots)
        #self.checkAvailableField(slots)

        for slot in slots:
            logging.debug('slot: %r', slot)
            if 'device' in slot:
                dev = slot['device']
                self.assertFalse(slot['available'])
                dtype = self.vm.command('qom-get', path=dev, property='type')
                self.assertTrue(any(self.typeImplements(dtype, t) for t in slot['device-types']))

            for dt in slot['device-types']:
                self.assertTrue(len(self.getUserCreatableSubtypes(dt)) > 0, "There's no user-creatable subtype of %s" % (dt))

            if slot['opts-complete']:
                self.assertTrue('count' in slot)

                all_counts = [len(ValuesIterator(p['values'])) for p in slot['opts']]
                total_count = reduce(operator.mul, all_counts, 1)
                logging.debug('%d possible values', total_count)
                self.assertEquals(total_count, slot['count'])

        self.vm.shutdown()

    def machineTestSlotInfo(self):
        if self.machine['name'] in BLACKLIST:
            self.skipTest("machine %s on BLACKLIST" % (self.machine['name']))

        args = ['-S', '-machine', self.machine['name']]
        self.checkSlotInfo(args)

        #TODO: also test using full config from docs/config/q35-*.cfg

    @classmethod
    def addMachineTest(klass, method_name, binary, machine):
        """Dynamically add a testMachine_<arch>_<name>_<machine> method to the class"""
        method = getattr(klass, method_name)
        def testMachine(self):
            self.binary = binary
            self.machine = machine
            return method(self)
        machine_name = machine['name'].replace('-', '_').replace('.', '_')
        method_name = 'test_%s_%s_%s' % (method_name, machine['arch'], machine_name)
        setattr(klass, method_name, testMachine)
        return method_name


    @classmethod
    def discoverMachines(klass, binary):
        """Run query-machines

        This method is run before test cases are started, so we
        can dynamically add test cases for each machine supported
        by the binary.
        """
        vm = qtest.QEMUQtestMachine(binary=binary, args=['-S', '-machine', 'none'])
        vm.launch()
        try:
            arch = vm.qmp('query-target')['return']['arch']
            machines = vm.qmp('query-machines')['return']
            for m in machines:
                m['arch'] = arch
        finally:
            vm.shutdown()
        return machines

    @classmethod
    def addMachineTests(klass, binary):
        """Dynamically add test methods for each machine found on QEMU binary

        Look for all methods with "machineTest" prefix, and add
        custom test methods that will test them, for each machine-type
        found on QEMU binary 'binary'.
        """
        method_names = unittest.loader.getTestCaseNames(klass, prefix='machineTest')
        machines = klass.discoverMachines(binary)
        for machine in machines:
            for mname in method_names:
                klass.addMachineTest(mname, binary, machine)


if os.getenv('QTEST_QEMU_BINARY'):
    QueryMachinesTest.addMachineTests(os.getenv('QTEST_QEMU_BINARY'))

if __name__ == '__main__':
    if os.getenv('QTEST_LOG_LEVEL'):
        logging.basicConfig(level=int(os.getenv('QTEST_LOG_LEVEL')))
    else:
        logging.basicConfig(level=logging.WARN)
    unittest.main()
