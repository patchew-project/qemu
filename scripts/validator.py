#!/usr/bin/env python
#
#  Copyright (c) 2018 Red Hat Inc
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

"""
QEMU validator script
=====================

This script will get test YAML test case specifications or Python
modules as input, and generate/run test cases based on them.

USAGE
-----

validator.py <specification-file>... -V VAR1=value1 VAR1=value2 VAR2=value3

specification-file is a YAML file containing the test specification.

Example::

    # this test specification is equivalent to the
    # "device/introspect/list" test case in device-introspect-test.c
    command-line: '$QEMU -nodefaults -machine none'
    monitor-commands:
    - qmp:
      - execute: qom-list-types
        arguments:
          implements: 'device'
          abstract: true
    - hmp: 'device_add help'


VARIABLE EXPANSION
------------------

The test runner will try to run the test cases with all possible values
for variables appearing in the test specification.

Some built-in variables are automatically expanded:

* `$MACHINE` - Expands to a machine-type name supported by $QEMU
* `$ACCEL` - Expands to an accelerator name supported by $QEMU
* `$DEVICE` - Expands to a (user-creatable) device type name supported by $QEMU
* `$CPU` - Expands to a CPU model name supported by $QEMU

Note that the $QEMU variable must be specified in th

TEST SPECIFICATION FIELDS
-------------------------

command-line
~~~~~~~~~~~~

List or string, containing the QEMU command-line to be run.

Default: '$QEMU'


monitor-commands
~~~~~~~~~~~~~~~~

Mapping or list-of-mappings containing monitor commands to run.  The key on each
item can be ``hmp`` or ``qmp``.  The value on each entry can be a string,
mapping, or list.

Default: None.

TODO: not implemented yet.


qmp
~~~

Boolean.  If true (the default), a QMP monitor is configured on the command-line
automatically.

If true, the test runner will issue a ``quit`` command automatically when
testing is finished.  If false, the test runner will wait until QEMU exits by
itself.

Example::

    # just run $QEMU -help and ensure it won't crash
    command-line: ['$QEMU', '-help']
    qmp: false


TODO: whitelist
TODO: validate output against reference output
TODO: configure defaults for variables
TODO: compatibility with Avocado multiplexer?
"""

import sys
import os
import string
import argparse
import pprint
import yaml
import logging
import shlex
import pipes
import re
import itertools
import traceback
import socket
from collections import OrderedDict

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'scripts'))
from qemu import QEMUMachine
from qmp.qmp import QMPError

logger = logging.getLogger('qemu.tests.validator')
dbg = logger.debug

# Python 2.7 compatibility:
shquote = getattr(shlex, 'quote', pipes.quote)

class InvalidSpecification(Exception):
    pass

def qom_type_names(vm, **kwargs):
    """Run qom-list-types QMP command, return type names"""
    types = vm.command('qom-list-types', **kwargs)
    return [t['name'] for t in types]


def info_qdm(vm):
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


class QemuBinaryInfo(object):
    """Information for a specific QEMU binary"""
    def __init__(self, binary):
        """Don't instantiate this directly, use get_binary_info()"""
        self.binary = binary

        args = ['-S', '-machine', 'none,accel=kvm:tcg']
        dbg("querying info for QEMU binary: %s", binary)
        vm = QEMUMachine(binary=binary, args=args)
        vm.launch()
        try:
            self.alldevs = qom_type_names(vm, implements='device', abstract=False)
            # there's no way to query DeviceClass::user_creatable using QMP,
            # so use 'info qdm':
            self.no_user_devs = [d['name'] for d in info_qdm(vm, ) if d['no-user']]
            self.machines = [m['name'] for m in vm.command('query-machines')]
            self.user_devs = [dev for dev in self.alldevs if dev not in self.no_user_devs]
            self.kvm_available = vm.command('query-kvm')['enabled']
            self.cpu_models = [c['name'] for c in vm.command('query-cpu-definitions')]
        finally:
            vm.shutdown()

    def available_accels(self):
        if self.kvm_available:
            yield 'kvm'
        yield 'tcg'

BINARY_INFO = {}

def get_binary_info(binary):
    """Lookup info for QEMU binary, caching data"""
    if binary not in BINARY_INFO:
        BINARY_INFO[binary] = QemuBinaryInfo(binary)
    return BINARY_INFO[binary]


# TEST CASE GENERATION LOGIC
#TODO: the functions below are not used yet, refactor code to use
#      the simpler variable expansion logic

def updatedict(d1, d2):
    """Return updated dictionary, after d1.update(d2)"""
    d = d1.copy()
    d.update(d2)
    return d

def newdict(d, k, v):
    """Return new dictionary with d[k] = v

    >>> a = {'a':1, 'b':2}
    >>> b = newdict(a, 'a', 100)
    >>> sorted(a.items())
    [('a', 1), ('b', 2)]
    >>> sorted(b.items())
    [('a', 100), ('b', 2)]
    """
    d = d.copy()
    d[k] = v
    return d

def mapchain(fn, l):
    """map(fn, l) and then chain the results together

    >>> multiples = lambda i: [i, i*2, i*3]
    >>> list(mapchain(multiples, [2, 3, 5]))
    [2, 4, 6, 3, 6, 9, 5, 10, 15]
    """
    return itertools.chain(*map(fn, l))

def call_func(v):
    """Call function, if v is callable"""
    if callable(v):
        return v()
    return v

def fnitem(tc, var, fn):
    """Replace tc[var] with fn(tc[var])"""
    return newdict(tc, var, fn(tc[var]))

def call_var_func(tc, var):
    """Update `var` with result of its enumeration func, if necessary

    >>> retlist = lambda tc: [{'a':[1,2,3], 'c':['hi']}]
    >>> tc = {'a':retlist, 'b':[100]}
    >>> tc = call_var_func(tc, 'a')
    >>> list(tc) == [{'a':[1,2,3], 'b':[100], 'c':['hi']}]
    True
    """
    if callable(tc[var]):
        r = tc[var](tc)
        assert all(var in t for t in r)
        return (updatedict(tc, t) for t in r)
    else:
        return [tc]

#def simple_enum(fn):
#    """Wrapper for simple enumeration functions that return a list of values
#
#    >>> fn = simple_enum(lambda: [1,2,3])
#    >>> tc = {'a':fn}
#    >>> tc = call_var_func(tc, 'a')
#    >>> tc['a']
#    [1, 2, 3]
#    """
#    def enum_func(tc):
#        return {}

def split_var(tc, var):
    """Split test case into multiple items

    >>> tc = {'a':[1,2,3], 'b':100}
    >>> tc = split_var(tc, 'a')
    >>> [r['a'] for r in tc]
    [[1], [2], [3]]
    """
    return (newdict(tc, var, [v]) for v in tc[var])

def split_vars(tc, vars):
    """Split test case for multiple variables

    >>> list1 = [1,2]
    >>> list2 = [10, 20]
    >>> tc = {'a':list1, 'b':list2, 'c':[100, 200, 300]}
    >>> tc = split_vars(tc, ['a', 'b'])
    >>> list(tc) == [{'a':[i], 'b':[j], 'c':[100, 200, 300]} for i in [1,2] for j in [10, 20]]
    True
    """
    tcs = [tc]
    for var in vars:
        #TODO: call_var_func()
        tcs = mapchain(lambda tc: split_var(tc, var), tcs)
    return tcs

class VariableDefinition(object):
    # variable names that must be set before this variable
    deps = []

    def __init__(self):
        self.values = None

    @staticmethod
    def enumerate(values):
        """Enumerate possible values for the variable"""
        raise NotImplementedError

class BuiltinVars(object):
    """Namespace for builtin variables"""
    class QEMU(VariableDefinition):
        """QEMU binary path, must be set explicitly by the user"""
        def enumerate(self, values):
            raise Exception("QEMU variable not set")

    class MACHINE(VariableDefinition):
        """Machine-type name.  Don't use with $MACHINE_OPT"""
        deps = ['QEMU']

        @staticmethod
        def enumerate(values):
            return get_binary_info(values['QEMU']).machines

    class ACCEL(VariableDefinition):
        deps = ['QEMU']

        @staticmethod
        def enumerate(values):
            return get_binary_info(values['QEMU']).available_accels()

    class DEVICE(VariableDefinition):
        deps = ['QEMU']

        @staticmethod
        def enumerate(values):
            return get_binary_info(values['QEMU']).user_devs

    class CPU(VariableDefinition):
        deps = ['QEMU']

        @staticmethod
        def enumerate(values):
            return get_binary_info(values['QEMU']).cpu_models

class VariableEnumerator(object):
    """Helper class that will enumerate possible values for variables"""
    def __init__(self):
        # start with built-in variables:
        self._vars = dict((v, getattr(BuiltinVars, v)()) for v in dir(BuiltinVars) if not v.startswith('_'))
        self._defaults = {}

    def _add_var(self, var):
        return self._vars.setdefault(var, VariableDefinition())

    def set_values(self, var, values):
        """Set default values for variable `var`

        `values` can be a list of values, or a single item
        Default values override the values returned by VariableDefinition.enumerate()
        """
        if not isinstance(values, list):
            values = [values]
        self._add_var(var).values = values

    def update_values(self, valuedict):
        dbg("update_values: %r", valuedict)
        for k,v in valuedict.items():
            self.set_values(k, v)

    def lookup_var(self, var):
        """Lookup variable"""
        return self._vars.get(var)

    def var_deps(self, var):
        """Return variables need to be set before `var`"""
        return self.lookup_var(var).deps

    def order_deps(self, vars):
        """Return full list of variables, including dependencies in the right order
        """
        dbg("START order_deps %r", vars)
        result = OrderedDict()
        vars = OrderedDict(((v,1) for v in reversed(vars)))
        while vars:
            dbg("queue: %r", list(vars.keys()))
            v,_ = vars.popitem(last=True)
            dbg("next var: %s", v)
            deps = self.var_deps(v)
            dbg("deps: %r", deps)
            if all(dep in result for dep in deps):
                dbg("var %s is ready", v)
                result[v] = 1
                continue
            # dependencies not met:
            vars[v] = 1
            for dep in deps:
                if dep in vars:
                    raise Exception("Variable dependency cycle: %s" % (' -> '.join(vars.keys())))
                vars[dep] = 1
        return list(result.keys())

    def enumerate(self, env, var, values):
        """Enumerate possible values for variable `var`

        May be called only if all requirements for `var` are set in `values`.
        """
        v = self.lookup_var(var)
        assert all(dep in values for dep in v.deps)
        dbg("var: %r", v)
        if v.values:
            r = v.values
        else:
            r = v.enumerate(values)
        logger.debug('Values for %s: %r', var, r)
        return r

# HELPER FUNCTIONS FOR TEMPLATE STRINGS:

def apply_template(templ, values):
    """Apply variables to a template, supporting strings and lists

    >>> apply_template('$QEMU -machine X', {'QEMU':'qemu-system-x86_64'})
    'qemu-system-x86_64 -machine X'
    >>> apply_template({"$TEST": ["$FOO", "is $BAR"]}, \
                       {'FOO':'XX', 'BAR':'YY', 'TEST':'TT'})
    {'$TEST': ['XX', 'is YY']}
    """
    if isinstance(templ, str):
        return string.Template(templ).substitute(values)
    elif isinstance(templ, list):
        return [apply_template(s, values) for s in templ]
    elif isinstance(templ, dict):
        return dict( (k, apply_template(v, values)) for (k, v) in templ.items())
    else:
        return templ

def vars_for_template(templ):
    """Return list of variables used by s when used as template string

    >>> vars_for_template('abcde fgh')
    []
    >>> vars_for_template('$A is ${A}, not ${B} or $C')
    ['A', 'B', 'C']
    >>> vars_for_template(['$QEMU', '-machine' , '$MACHINE$MACHINE_OPT'])
    ['QEMU', 'MACHINE', 'MACHINE_OPT']
    """
    usedKeys = OrderedDict()
    class LoggingDict(object):
        def __getitem__(self, k):
            usedKeys[k] = 1
            return 'X'
    apply_template(templ, LoggingDict())
    return list(usedKeys.keys())

class TestSpecification(object):
    def __init__(self, data):
        self._data = data
        self.normalize()

    def normalize(self):
        """Normalize test specification data

        * ensure 'command-line' is a list of arguments
        * 'monitor-commands' will be an array
        """
        # if command-line is omitted, just run QEMU with no arguments:
        self._data.setdefault('command-line', ['$QEMU'])
        self._data.setdefault('monitor-commands', [])

        # 'monitor-commands' must be a list
        if not isinstance(self.get('monitor-commands'), list):
            self._data['monitor-commands'] = [self.get('monitor-commands')]

    @classmethod
    def load_file(cls, file):
        data = yaml.load(open(file))
        return cls(data)

    def get(self, key, default=None):
        return self._data.get(key, default)

    def _gen_subtests(self, testcases, env, vars):
        """Call _gen_test_cases() for each test case in `testcases`"""
        #import pdb; pdb.set_trace()
        for tc in testcases:
            for st in self._gen_test_cases(env, vars, tc):
                yield st

    def _gen_var_testcases(self, env, var, values):
        """Generate one test case for each possible value for `var`"""
        for value in self.var_enum.enumerate(env, var, values):
            newvalues = values.copy()
            newvalues[var] = value
            yield newvalues

    def _gen_test_cases(self, env, vars, values):
        """Generate list of test cases

        :param vars: List of variable names that are not set yet
        :param values: values of variables that are already set
        """
        if not vars:
            # No unset variables -> only 1 test case
            return [values.copy()]

        # pick next unset variable, enumerate values, set it:
        var = vars[0]
        cases = self._gen_var_testcases(env, var, values)
        cases = self._gen_subtests(cases, env, vars[1:])
        return cases

    def gen_test_cases(self, env):
        """Generate all test cases for this test specification"""
        # we generate combinations for the command-line and monitor commands
        # in separate steps, so test cases using the same QEMU command-line
        # are grouped together
        vars = vars_for_template(self.get('command-line')) + vars_for_template(self.get('monitor-commands'))

        self.var_enum = VariableEnumerator()
        if not env.args.full:
            self.var_enum.update_values(self.get('defaults', {}))
        self.var_enum.update_values(env.var_values)

        # put dependencies in right order:
        vars = self.var_enum.order_deps(vars)
        cases = self._gen_test_cases(env, vars, {})
        return (TestCase(self, c) for c in cases)

class TestCase(object):
    def __init__(self, spec, values):
        self.spec = spec
        self.values = values

    def __str__(self):
        return ' '.join('%s=%s' % (k, shquote(v)) for k,v in self.values.items())

    def is_expected_entry(self, expected_entry):
        """Check if `expected_entry` matches the testcase/results"""
        expected_vars = expected_entry.copy()
        for var,value in expected_vars.items():
            if self.values.get(var) != value:
                return False
        return True

    def is_expected_failure(self):
        for e in self.getField('expected-failures', []):
            if self.is_expected_entry(e):
                return True

    def getField(self, var, default=None):
        """Get value of test spec field, expanding variables"""
        return apply_template(self.spec.get(var, default), self.values)

    def qmp_cmd(self, vm, cmd):
        if isinstance(cmd, list):
            for c in cmd:
                self.qmp_cmd(vm, c)
        elif isinstance(cmd, dict):
            return vm.qmp_obj(cmd)
        else:
            raise InvalidSpecification("QMP command must be dict: %r" % (cmd))

    def hmp_cmd(self, vm, cmd):
        return vm.command('human-monitor-command', command_line=cmd)

    def monitor_cmd(self, vm, cmd):
        dbg("monitor cmd: %r", cmd)
        if isinstance(cmd, dict):
            for k,v in cmd.items():
                if k == 'qmp':
                    self.qmp_cmd(vm, v)
                elif k == 'hmp':
                    self.hmp_cmd(vm, v)
                else:
                    raise InvalidSpecification("Invalid monitor command: %r: %r" % (k, v))

    def run(self, env):
        """Check one specific test case

        Returns a dictionary containing failure information on error,
        or None on success
        """
        result = {'success': True }
        result['is-expected-failure'] = self.is_expected_failure()

        cmdline = self.getField('command-line')
        if not isinstance(cmdline, list):
            cmdline = shlex.split(cmdline)

        qmp = self.getField('qmp', True)
        #TODO: use context manager to enter/exit borrowed VM from env
        vm = env.get_vm(cmdline, qmp)
        try:
            if not vm.is_launched():
                vm.launch()
            #TODO: generate/enumerate variables inside monitor commands too
            for cmd in self.getField('monitor-commands', []):
                self.monitor_cmd(vm, cmd)
            if not qmp:
                vm.wait()
                env.drop_vm()
        except KeyboardInterrupt:
            raise
        except QMPError as err:
            result['exception'] = repr(err)
            result['success'] = False
        except socket.error as err:
            result['exception'] = repr(err)
            result['success'] = False

        dbg('vm is %r', vm)
        ec = vm.exitcode()
        dbg("exit code: %r", ec)
        if ec is not None and ec != 0:
            result['success'] = False
        result['exitcode'] = ec
        result['log'] = vm.get_log()

        #TODO: use context manager to enter/exit borrowed VM from env
        if not result['success']:
            env.drop_vm()

        return result


class TestEnv(object):
    def __init__(self, args):
        self.args = args
        self._last_vm_args = None
        self._last_vm = None

    def qemu_binaries(self):
        return self.args.qemu_binaries

    def drop_vm(self):
        """Drop existing VM object"""
        if self._last_vm:
            #TODO: record failures here
            self._last_vm.shutdown()
            self._last_vm = None
            self._last_vm_args = None

    def get_vm(self, cmdline, qmp):
        """Get VM object for test case"""
        if self._last_vm_args == (cmdline, qmp) and self._last_vm.is_running():
            dbg("Reusing VM object for cmdline %r", cmdline)
            return self._last_vm

        dbg("Starting new VM for cmdline %r", cmdline)
        #FIXME: need to catch exitcode/segfaults here somehow  :(
        self.drop_vm()
        vm = QEMUMachine(binary=cmdline[0], args=cmdline[1:], qmp=qmp)
        self._last_vm = vm
        self._last_vm_args = (cmdline, qmp)
        return vm

def main():
    parser = argparse.ArgumentParser(description="Generic QEMU validator")
    parser.set_defaults(loglevel=logging.INFO)
    parser.add_argument('-V', metavar='VAR=VALUE', nargs='*',
                        help="Force variabie VAR to VALUE",
                        action='append', dest='vars', default=[])
    parser.add_argument('-d', '--debug',action='store_const',
                        dest='loglevel', const=logging.DEBUG,
                        help='debug output')
    parser.add_argument('-v', '--verbose',action='store_const',
                        dest='loglevel', const=logging.INFO,
                        help='verbose output')
    parser.add_argument('-q', '--quiet',action='store_const',
                        dest='loglevel', const=logging.WARN,
                        help='non-verbose output')
    parser.add_argument("--dry-run", action="store_true",
                        help="Don't run test cases")
    parser.add_argument("--full", action="store_true",
                        help="Run all test case combinations, not just the default for the test specification")
    parser.add_argument("testfiles", nargs="+", metavar="FILE",
                        help="Load test case specification from FILE")
    args = parser.parse_args()

    env = TestEnv(args)

    vars = {}
    if args.vars:
        for varval in itertools.chain(*args.vars):
            var,val = varval.split('=', 1)
            vars.setdefault(var, []).append(val)
    env.var_values = vars

    logging.basicConfig(stream=sys.stdout, level=args.loglevel, format='%(levelname)s: %(message)s')
    resultdict = {}
    try:
        for testfile in args.testfiles:
            specname = os.path.basename(testfile)
            #TODO: support test specifications pointing to Python modules
            spec = TestSpecification.load_file(testfile)
            logger.debug("Test specification:")
            logger.debug(pprint.pformat(spec._data))
            logger.debug('---')
            for tc in spec.gen_test_cases(env):
                if tc.is_expected_failure():
                    logger.info("%s: Skipped: %s", specname, str(tc))
                    continue
                logger.info("%s: Running: %s", specname, str(tc))
                if not args.dry_run:
                    r = tc.run(env)
                    logger.debug("Result:")
                    logger.debug(pprint.pformat(r))
                    if not r['success']:
                        logger.error("%s: failed: %s", specname, tc)
                    resultdict.setdefault(r['success'], []).append( (tc, r) )
    except KeyboardInterrupt:
        # Print partial test result summary on interrupt
        logger.info("Interrupted. Partial test summary follows")
        pass

    env.drop_vm()

    if not args.dry_run:
        logger.info('%d successes', len(resultdict.get(True, [])))
        failures = resultdict.get(False, [])
        if failures:
            logger.error('%d failures', len(failures))
            for tc,r in failures:
                logger.error("Failed: %s", tc)
                logger.error("Result:")
                pprint.pprint(r)
                dbg("Result: %r", r)

if __name__ == '__main__':
    sys.exit(main())
