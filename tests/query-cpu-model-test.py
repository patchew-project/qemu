#!/usr/bin/env python
#
# query-cpu-model-* validation and sanity checks
#
#  Copyright (c) 2016-2017 Red Hat Inc
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

import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'scripts'))
from qtest import QEMUQtestMachine
import unittest
import logging
import copy

UNSAFE_FEATURES = {
    'x86_64': ['pmu', 'host-cache-info'],
    'i386': ['pmu', 'host-cache-info'],
}

# testing every single CPU model takes a while, so test just a few ones:
X86_MODELS_TO_TEST = set(['base', 'host',
                          'qemu64', 'qemu32', 'kvm64', 'kvm32',
                          '486', 'pentium', 'athlon', 'n270',
                          'Haswell-noTSX', 'Haswell', 'Broadwell',
                          'Opteron_G5'])

MODELS_TO_TEST = {
    'x86_64': X86_MODELS_TO_TEST,
    'i386': X86_MODELS_TO_TEST,
}

# Validation of the expanded CPU models will be based on the QOM
# properties of CPU objects.
# QOM properties that don't affect guest ABI can be safely ignored
# when validating the results.
IGNORE_QOM_PROPS = {
    # the 'type' property just identifies the QOM class being used
    # to build the CPU, and shouldn't affect the guest ABI
    'x86_64': ['type'],
    'i386': ['type'],
    # 'static', 'migration-safe', and 'description' are just
    # information for the user, and don't affect guest ABI
    's390x': ['type', 'static', 'migration-safe', 'description'],
}

def toQemuOpts(*args):
    """Convert arguments to a QemuOpts string, with appropriate escaping

    Each argument can be a single string, or a dictionary.
    """
    logging.debug('toQemuOpts(%r)', args)
    r = []
    for a in args:
        if type(a) is dict:
            for k,v in a.items():
                if type(v) is bool:
                    if v:
                        v = 'on'
                    else:
                        v = 'off'
                v = str(v)
                a = '%s=%s' % (k, v)
                r.append(a)
        else:
            a = str(a)
            r.append(a)
    return ','.join([o.replace(',', ',,') for o in r])

def cpuPath(vm, cpu_index):
    """Return qom_path for a given CPU, using query-cpus"""
    cpus = vm.command('query-cpus')
    return cpus[cpu_index]['qom_path']

def allProps(vm, path):
    """Return a dictionary containing all properties for a QOM object"""
    props = vm.command('qom-list', path=path)
    r = {}
    for prop in props:
        pname = prop['name']
        v = vm.command('qom-get', path=path, property=pname)
        r[pname] = v
    return r

def allCpuProps(vm, cpu_index):
    """Return all properties for a given CPU"""
    return allProps(vm, cpuPath(vm, cpu_index))


class CPUModelTest(unittest.TestCase):
    longMessage = True
    maxDiff = None

    def runAndGetProps(self, model):
        # Helper to run QEMU using a CpuModelInfo struct and get
        # all CPU properties
        cpu_opt = toQemuOpts(model['name'], model.get('props', {}))
        logging.debug('cpu option: %s', cpu_opt)

        vm = QEMUQtestMachine(args=['-machine', 'accel=%s' % (self.accel), '-S',
                                    '-cpu', cpu_opt], name='qom-fetch',
                              logging=False)
        try:
            vm.launch()
            props = allCpuProps(vm, 0)
        finally:
            vm.shutdown()

        # remove the properties we can ignore
        for p in IGNORE_QOM_PROPS.get(self.target['arch'], []):
            del props[p]

        return props

    def tryGetProps(self, model, msg):
        """Try to get QOM props for model, if runnable"""
        if model.has_key('qom-props'):
            return

        if model.get('runnable') != False:
            logging.info("%s: maybe runnable, fetching QOM properties", msg)
            try:
                model['qom-props'] = self.runAndGetProps(model['model'])
            except:
                if model.get('runnable'):
                    # explicitly marked as runnable, raise exception
                    raise
                logging.info("%s: failed to run VM, ignoring", msg)

    def unsafeFeatures(self):
        return UNSAFE_FEATURES.get(self.target['arch'], [])

    def isMigrationSafe(self, model):
        name = model['name']
        if not self.cpu_models[name]['migration-safe']:
            return False
        for p in model.get('props', {}).keys():
            if p in self.unsafeFeatures():
                return False
        return True

    def checkOneExpansion(self, model, type, msg):
        """Perform one query-cpu-model-expansion operation, validate results

        @model is a CpuModelExpansionInfo struct, with some extra keys:
        * model['runnable'] will be set to True if the CPU model is
          runnable on this host
        * model['qom-props'] will be set to the full list of properties for the
          CPU, if the model is runnable

        Returns a new CpuModelExpansion struct like @model, with
        the expanded CPU model data.
        """
        logging.info("%s: testing type=%s", msg, type)
        logging.debug("%s: model: %r", msg, model)

        model_name = model['model']['name']

        self.tryGetProps(model, msg)

        expanded = self.vm.command('query-cpu-model-expansion',
                                   type=type, model=model['model'])

        logging.debug("%s: expanded: %r", msg, expanded)

        # static expansion mode should always result in a static and
        # migration safe CPU model
        if type == 'static':
            expanded_model = self.cpu_models[expanded['model']['name']]
            self.assertTrue(self.isMigrationSafe(expanded_model))
            self.assertTrue(expanded_model['static'])

        # static expansion should never enable migration-unsafe
        # features:
        if type == 'static':
            for f in self.unsafeFeatures():
                self.assertFalse(expanded['model']['props'].get(f))

        # Some expansions are known to be precise, and shouldn't lose any
        # features:
        # * full expansion
        # * static expansion of a known migration-safe model
        precise_expansion = (type == 'full') or \
                            self.isMigrationSafe(model['model'])

        expanded['runnable'] = model.get('runnable')
        self.tryGetProps(expanded, msg)
        if precise_expansion:
            self.assertEquals(model.get('qom-props'),
                              expanded.get('qom-props'),
                              msg)

        logging.debug("%s: result: %r", msg, expanded)
        return expanded

    def checkExpansions(self, model, msg):
        """Perform multiple expansion operations on model, validate results

        @model is a CpuModelExpansionInfo struct, with some extra keys:
        * model['runnable'] should be set to True if the CPU model is
          runnable on this host
        * model['qom-props'] will be set to the full list of properties for
          the CPU, if the model is runnable
        """
        exp_s = self.checkOneExpansion(model, 'static',
                                       '%s.static' % (msg))
        exp_f = self.checkOneExpansion(model, 'full',
                                       '%s.full' % (msg))
        exp_ss = self.checkOneExpansion(exp_s, 'static',
                                        '%s.static.static' % (msg))
        exp_sf = self.checkOneExpansion(exp_s, 'full',
                                        '%s.static.full' % (msg))
        exp_ff = self.checkOneExpansion(exp_f, 'full',
                                        '%s.full.full' % (msg))

        # static expansion twice should result in the same data:
        self.assertEquals(exp_s, exp_ss, '%s: static != static+static' % (msg))
        # full expansion twice should also result in the same data:
        self.assertEquals(exp_f, exp_ff, '%s: full != full+full' % (msg))

        # migration-safe CPU models have an extra feature:
        # their static expansion should be equivalent to the full
        # expansion (as their static expansion is also precise)
        if self.isMigrationSafe(model['model']):
            self.assertEquals(exp_sf['model']['props'], exp_f['model']['props'],
                              '%s: props: static+full != full' % (msg))
            self.assertEquals(exp_sf.get('qom-props'), exp_f.get('qom-props'),
                              '%s: qom-props: static+full != full' % (msg))

    def tryToMakeRunnable(self, model):
        """Try to create a runnable version of the CPU model, by disabling
        unavailable features
        """
        devprops = self.vm.command('device-list-properties',
                                   typename=model['typename'])
        proptypes = dict((p['name'], p['type']) for p in devprops)

        props = {}
        for f in model['unavailable-features']:
            # try to disable only boolean properties:
            if proptypes.get(f) == 'bool':
                props[f] = False

        if not props:
            # no property found to be disabled, there's nothing we can do
            return None

        runnable_model = {
            'model': {
                'name':  model['name'],
                'props': props,
            },
        }
        return runnable_model

    def commandAvailable(self, command):
        commands = self.vm.command('query-commands')
        names = set([c['name'] for c in commands])
        return command in names

    def checkOneCPUModel(self, m):
        """Run multiple query-cpu-model-expansion checks

        * Test simple CPU model name
        * Test CPU model with unsafe features explicitly disabled
          if it's not migration-safe
        * Test CPU model with unsafe features enabled
        * Test CPU model with unavailable features disabled,
          if unavailable-features is set

        @m is a CpuDefinitionInfo struct from query-cpu-definitions
        """
        msg = '%s.%s' % (self.accel, m['name'])
        logging.info("%s: checkOneCPUModel", msg)


        # some validations on query-cpu-definitions output:
        if m.get('static'):
            self.assertTrue(m['migration-safe'])

        # simulate return value of query-cpu-expansion for the model:
        model = {
            'model': {
                'name': m['name'],
            },
        }
        if m.has_key('unavailable-features'):
            model['runnable'] = len(m['unavailable-features']) == 0
        self.checkExpansions(model, msg)

        # explicit test to check we do the right thing when
        # unsafe features are enabled explicitly:
        for f in self.unsafeFeatures():
            # enabled:
            unsafe_model = {
                'model': {
                    'name':  m['name'],
                    'props': { f: True },
                },
                'runnable': model.get('runnable'),
            }
            self.checkExpansions(unsafe_model, msg + ".unsafe." + f)

        # Try to make CPU model migration-safe by disabling
        # all known migration-unsafe features:
        if not m['migration-safe']:
            # enabled:
            safe_model = {
                'model': {
                    'name':  m['name'],
                    'props': {}
                },
                'runnable': model.get('runnable'),
            }
            for f in self.unsafeFeatures():
                safe_model['model']['props'][f] = False
            self.checkExpansions(safe_model, msg + ".safe")

        # if not runnable, try to create a runnable version of the CPU model:
        if m.get('unavailable-features'):
            runnable_model = self.tryToMakeRunnable(m)
            if runnable_model:
                self.checkExpansions(runnable_model, msg + ".runnable")

    @classmethod
    def setUpClass(klass):
        vm = QEMUQtestMachine(args=['-S'], logging=False)
        try:
            vm.launch()
            klass.kvm = vm.command('query-kvm')
            klass.target = vm.command('query-target')
        finally:
            vm.shutdown()

    def setUp(self):
        self.vm = None

    def tearDown(self):
        if self.vm:
            self.vm.shutdown()

    def checkAllCPUModels(self):
        # use <accel>:tcg so QEMU won't refuse to start if KVM is unavailable
        self.vm = QEMUQtestMachine(args=['-S', '-machine',
                                    'accel=%s:tcg' % (self.accel)],
                              logging=False)
        self.vm.launch()

        models = self.vm.command('query-cpu-definitions')
        self.cpu_models = dict((m['name'], m) for m in models)

        if self.accel == 'kvm':
            if not self.vm.command('query-kvm')['enabled']:
                self.skipTest("Failed to enable KVM")

        to_test = MODELS_TO_TEST.get(self.target['arch'])
        for m in models:
            if to_test and m['name'] not in to_test:
                continue
            self.checkOneCPUModel(m)


    def testTCGModels(self):
        self.accel = 'tcg'
        self.checkAllCPUModels()

    def testKVMModels(self):
        if not self.kvm['present']:
            self.skipTest("KVM is not present")

        self.accel = 'kvm'
        self.checkAllCPUModels()



if __name__ == '__main__':
    if os.getenv('QTEST_LOG_LEVEL'):
        logging.basicConfig(level=int(os.getenv('QTEST_LOG_LEVEL')))
    elif '--verbose' in sys.argv:
        logging.basicConfig(level=logging.INFO)
    else:
        logging.basicConfig(level=logging.WARN)
    unittest.main()
