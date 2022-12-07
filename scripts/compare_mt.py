#!/usr/bin/env python3
#
# Script to compare machine type compatible properties (include/hw/boards.h).
# compat_props are applied to the driver during initialization to change
# default values, for instance, to maintain compatibility.
# This script constructs table with machines and values of their compat_props
# to compare and to find places for improvements or places with bugs. If
# during the comparison, some machine type doesn't have a property (it is in
# the comparison table because another machine type has it), then the
# appropriate method will be used to obtain the default value of this driver
# property via qmp command (e.g. query-cpu-model-expansion for x86_64-cpu).
# These methods are defined below in qemu_property_methods.
#
# Copyright (c) Yandex Technologies LLC, 2022
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
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.

from tabulate import tabulate
import sys
from os import path
from argparse import ArgumentParser, RawTextHelpFormatter, Namespace
import pandas as pd
from typing import Callable, List, Dict, Generator, Tuple, Union, Any, Set

try:
    qemu_dir = path.abspath(path.dirname(path.dirname(__file__)))
    sys.path.append(path.join(qemu_dir, 'python'))
    from qemu.machine import QEMUMachine
except ModuleNotFoundError as exc:
    print(f"Module '{exc.name}' not found.")
    print("Try export PYTHONPATH=top-qemu-dir/python or run from top-qemu-dir")
    sys.exit(1)


default_cmd_line = 'build/qemu-system-x86_64 -enable-kvm -machine none'


##### Methods for gettig the right values of drivers properties
#
# Use these methods as a 'whitelist' and add entries only if necessary. It's
# important to be stable and predictable in analysis and tests.
# Be careful:
# * Class must be inherited from 'PropQEMUObject' and its name has to start
#   with 'PropQEMU'
# * Class constructor must set appropriate QEMU type name via QEMUObject
#   constructor, the name must be in qom-list-types format (486-x86_64-cpu,
#   not 486)
# * Specialization always wins (from 'device' and 'x86_64-cpu', 'x86_64-cpu'
#   will be used for '486-x86_64-cpu')

# It's default stub for all undefined in prop_methods drivers because all
# QEMU types are inherited from Object
class PropQEMUObject():
    def __init__(self, name: str) -> None:
        self.name = name

    def get_prop(self, vm: QEMUMachine, device: str, prop_name: str) -> str:
        return 'Unavailable method'


class PropQEMUDevice(PropQEMUObject):
    def __init__(self) -> None:
        super().__init__('device')
        self.cached: Dict[str, List[Dict[str, Any]]] = {}

    def get_prop(self, vm: QEMUMachine, device: str, prop_name: str) -> str:
        if device not in self.cached:
            self.cached[device] = vm.command('device-list-properties',
                                             typename=device)
        for prop in self.cached[device]:
            if prop['name'] == prop_name:
                return str(prop.get('default-value', 'No default value'))

        return 'Unknown property'


class PropQEMUx86CPU(PropQEMUObject):
    def __init__(self) -> None:
        super().__init__('x86_64-cpu')
        self.cached: Dict[str, Dict[str, Any]] = {}


    def get_prop(self, vm: QEMUMachine, device: str, prop_name: str) -> str:
        if not device.endswith('-x86_64-cpu'):
            return 'Wrong x86_64-cpu name'

        # crop last 11 chars '-x86_64-cpu'
        name = device[:-11]
        if name not in self.cached:
            self.cached[name] = vm.command('query-cpu-model-expansion',
                                           type='full',
                                           model={'name': name}
                                           )['model']['props']
        return str(self.cached[name].get(prop_name, 'Unknown property'))


# Now it's stub, because all memory_backend types don't have default values
# but this behaviour can be changed
class PropQEMUMemoryBackend(PropQEMUObject):
    def __init__(self) -> None:
        super().__init__('memory-backend')
        self.cached: Dict[str, List[Dict[str, Any]]] = {}


    def get_prop(self, vm: QEMUMachine, driver: str, prop_name: str) -> str:
        if driver not in self.cached:
            self.cached[driver] = vm.command('qom-list-properties',
                                             typename=driver)
        for prop in self.cached[driver]:
            if prop['name'] == prop_name:
                return str(prop.get('default-value', 'No default value'))

        return 'Unknown property'


# contains all property classes to associate them to the appropriate QEMU types
class QEMUPropsAggregator():
    def __init__(self) -> None:
        self.property_methods = {}
        self.default = PropQEMUObject('object')

        for name, obj in globals().items():
            if not name.startswith('PropQEMU'):
                continue

            # base class
            if name == ('PropQEMUObject'):
                continue

            prop_obj = obj()
            self.property_methods[prop_obj.name] = prop_obj


    def get_class(self, driver_name: str) -> PropQEMUObject:
        return self.property_methods.get(driver_name, self.default)

##### End of methods definition


# implements description of a QEMU driver
class Driver:
    def __init__(self, driver_defs: dict, driver_name: str, parent_name: str,
                 is_abstr: bool, list_of_children: List[str],
                 get_method: PropQEMUObject) -> None:
        self.driver_defs = driver_defs
        self.name = driver_name
        self.parent = parent_name
        self.abstract = is_abstr
        self.children = list_of_children
        self.method = get_method


    # checks whether the driver is parent
    def is_parent(self, driver_name: str) -> bool:
        if driver_name not in self.driver_defs:
            return False

        cur_parent = self.parent
        while cur_parent:
            if driver_name == cur_parent:
                return True
            cur_parent = self.driver_defs[cur_parent].parent

        return False


    # set property getting class based on QEMU types structure
    def set_prop_method(self, prop_method: PropQEMUObject) -> None:
        if prop_method.name != self.name:
            return

        self.method = prop_method
        if not self.abstract:
            return

        for child in self.children:
            if not self.driver_defs[child].method:
                self.driver_defs[child].method = prop_method
                continue

            # specialization always wins
            if self.is_parent(self.driver_defs[child].method.name):
                self.driver_defs[child].method = prop_method


# implements the relationship between drivers and how to get their properties
class DriverDefinitions:
    def __init__(self, vm: QEMUMachine) -> None:
        self.driver_defs: Dict[str, Driver] = {}
        self.methods = QEMUPropsAggregator()
        self.vm = vm

        qom_all_types = vm.command('qom-list-types', abstract=True)
        for obj_type in qom_all_types:
            # parent of Object is None
            parent = obj_type.get('parent', None)
            abstr = obj_type.get('abstract', False)
            name = obj_type['name']
            if not abstr:
                self.driver_defs[name] = Driver(self.driver_defs, name, parent,
                                                abstr, [],
                                                self.methods.get_class(name))
                continue

            list_child_objs = vm.command('qom-list-types', implements=name,
                                         abstract=True)
            child_list = [child['name'] for child in list_child_objs]
            self.driver_defs[name] = Driver(self.driver_defs, name, parent,
                                            abstr, child_list,
                                            self.methods.get_class(name))

        # associating property getting class and QEMU types based on QEMU
        # structure from 'qom-list-types'
        for driver_name, prop_method in self.methods.property_methods.items():
            # skip other architectures and etc.
            if prop_method.name not in self.driver_defs:
                continue
            self.driver_defs[driver_name].set_prop_method(prop_method)


    # adds default values of the driver property to the given list. If the
    # driver is abstract all possible default values of derived classes will
    # be added
    def add_prop_value(self, driver: str, prop: str,
                       prop_list: List[Any]) -> None:
        # wrong driver name or disabled in config driver
        if driver not in self.driver_defs:
            prop_list.append('Unavailable driver')
            return

        if not self.driver_defs[driver].abstract:
            prop_list.append(self.driver_defs[driver].method.get_prop(self.vm,
                                                                      driver,
                                                                      prop))
            return

        # if abstract we need to collect default values from all children
        values = set()
        for child in self.driver_defs[driver].children:
            if self.driver_defs[child].abstract:
                continue

            values.add(self.driver_defs[child].method.get_prop(self.vm, child,
                                                               prop))

        prop_list.append(list(values))


# short QEMU machine type description. It contains only compat_props
class Machine:
    # raw_mt_dict - dict produced by `query-machines`
    def __init__(self, raw_mt_dict: Dict[str, Any]) -> None:
        self.name = raw_mt_dict['name']
        self.compat_props: Dict[str, Any] = {}
        # properties are applied sequentially and can rewrite values as in QEMU
        for prop in raw_mt_dict['compat-props']:
            if prop['driver'] not in self.compat_props:
                self.compat_props[prop['driver']] = {}
            self.compat_props[prop['driver']][prop['property']] = prop['value']


script_desc = """Script to compare machine types (their compat_props).

If a property applies to an abstract class this script collects default \
values of all child classes and prints them as a set.

"Unavailable method" - means that this script doesn't know how to get \
default values of the driver. To add method use the construction described \
at the top of the script.
"Unavailable driver" - means that this script doesn't know this driver. \
For instance, this can happen if you configure QEMU without this device or \
if machine type definition has error.
"No default value" - means that the appropriate method can't get the default \
value and most likely that this property doesn't have it.
"Unknown property" - means that the appropriate method can't find property \
with this name."""


def parse_args() -> Namespace:
    parser = ArgumentParser(formatter_class=RawTextHelpFormatter,
                            description=script_desc)
    parser.add_argument('--format', choices=['human-readable', 'json', 'csv'],
                        default='human-readable',
                        help='returns table in json format')
    parser.add_argument('--raw', action='store_true',
                        help='prints ALL defined properties without value '
                             'transformation. By default, only properties '
                             'with different values will be printed and with '
                             'value transformation(e.g. "on" -> True)')
    parser.add_argument('--cmd-line', default=default_cmd_line,
                        help='command line to start qemu. '
                             f'Default: "{default_cmd_line}"')

    mt_args_group = parser.add_mutually_exclusive_group()
    mt_args_group.add_argument('--all', action='store_true',
                               help='prints all available machine types (list '
                                    'of machine types will be ignored)')
    mt_args_group.add_argument('--mt', nargs="*", type=str,
                               help='list of Machine Types '
                                    'that will be compared')

    return parser.parse_args()


# returns socket_name, major version, minor version, revision
def mt_comp(mt: Machine) -> Tuple[str, int, int, int]:
    # none, microvm, x-remote and etc.
    if '-' not in mt.name or '.' not in mt.name:
        return mt.name, 0, 0, 0

    socket, ver = mt.name.rsplit('-', 1)
    ver_list = list(map(int, ver.split('.', 2)))
    ver_list += [0] * (3 - len(ver_list))
    return socket, ver_list[0], ver_list[1], ver_list[2]


# construct list of machine type definitions (primarily compat_props) from QEMU
def get_mt_definitions(vm: QEMUMachine) -> List[Machine]:
    raw_mt_defs = vm.command('query-machines', compat_props=True)
    mt_defs = []
    for raw_mt in raw_mt_defs:
        mt_defs.append(Machine(raw_mt))

    mt_defs.sort(key=mt_comp)
    return mt_defs


# returns list of requested by user machines
def get_req_mt(vm: QEMUMachine, args: Namespace) -> List[Machine]:
    mt_defs = get_mt_definitions(vm)
    if args.all:
        return mt_defs

    list_mt = [mt.name for mt in mt_defs]

    if args.mt is None:
                print('Enter machine types for comparision or use --help')
                print('List of available machine types:')
                print(*list_mt, sep='\n')
                sys.exit(1)

    for mt in args.mt:
        if mt not in list_mt:
            print('Wrong machine type name')
            print('List of available machine types:')
            print(*list_mt, sep='\n')
            sys.exit(1)

    requested_mt = []
    for mt in mt_defs:
        if mt.name in args.mt:
            requested_mt.append(mt)

    return requested_mt


# method to iterate through all requested properties in machine definitions
def get_req_props(mt_defs: List[Machine]) -> Generator[Tuple[str, str],
                                                       None, None]:
    driver_props: Dict[str, Set[Any]] = {}
    for mt in mt_defs:
        compat_props = mt.compat_props
        for driver, prop in compat_props.items():
            if driver not in driver_props:
                driver_props[driver] = set()
            driver_props[driver].update(prop.keys())

    for driver, props in sorted(driver_props.items()):
        for prop in sorted(props):
            yield driver, prop


def transform_value(value: str) -> Union[str, bool]:
    true_list = ['true', 'on']
    false_list = ['false', 'off']

    out = value.lower()

    if out in true_list:
        return True

    if out in false_list:
        return False

    return out


def transform_number(value: str) -> Union[int, None]:
    try:
        # C doesn't work with underscore ('2_5' != 25)
        if '_' in value:
            raise ValueError

        return int(value, 0)

    except ValueError:
        return None


# delete rows with the same values for all mt and transform values to make it
# easier to compare
def transform_table(table: Dict[str, List[Any]],
                    mt_names: List[str]) -> pd.DataFrame:
    new_table = {}
    for full_prop_name, prop_values in table.items():
        new_row = []
        all_values = set()
        # original number format if not all values are the same in the row
        numeric_values = set()
        for mt_prop_val in prop_values:
            if type(mt_prop_val) is list:
                transformed = list(map(transform_value, mt_prop_val))
                if len(transformed) == 1:
                    new_row.append(transformed[0])
                else:
                    new_row.append(transformed)

                numeric_values.update(set(map(transform_number, mt_prop_val)))
                all_values.update(set(transformed))
            else:
                transformed = transform_value(mt_prop_val)
                new_row.append(transformed)
                numeric_values.add(transform_number(mt_prop_val))
                all_values.add(transformed)

        if len(mt_names) > 1:
            if len(all_values) == 1:
                continue

            if None not in numeric_values and len(numeric_values) == 1:
                continue

        new_table[full_prop_name] = new_row

    return pd.DataFrame.from_dict(new_table, orient='index', columns=mt_names)


# constructs table in the format:
#
#                   | machine1 | machine2 | ...
# driver1:property1 |  value1  |  value2  | ...
# driver1:property2 |  value3  |  value4  | ...
# driver2:property1 |  value5  |  value6  | ...
#       ...         |   ...    |   ...    | ...
#
def fill_prop_table(mt_list: List[Machine],
                    qemu_drivers: DriverDefinitions,
                    is_raw: bool) -> pd.DataFrame:
    table: Dict[str, List[Any]] = {}
    for driver, prop in get_req_props(mt_list):
        name = f'{driver}:{prop}'
        table[name] = []
        for mt in mt_list:
            if driver in mt.compat_props:
                # values from QEMU machine type definitions
                if prop in mt.compat_props[driver]:
                    table[name].append(mt.compat_props[driver][prop])
                    continue

            # values from QEMU type definitions
            qemu_drivers.add_prop_value(driver, prop, table[name])

    headers = [mt.name for mt in mt_list]

    if is_raw:
        return pd.DataFrame.from_dict(table, orient='index', columns=headers)

    return transform_table(table, headers)


def print_table(table: pd.DataFrame, table_format: str) -> None:
    if table_format == 'json':
        print(comp_table.to_json())
    elif table_format == 'csv':
        print(comp_table.to_csv())
    else:
        print(tabulate(comp_table, showindex=True, stralign='center',
                       colalign=('left',), tablefmt='fancy_grid',
                       headers='keys', disable_numparse=True))


if __name__ == '__main__':
    args = parse_args()
    qemu_arg_list = args.cmd_line.split(' ')
    with QEMUMachine(binary=qemu_arg_list[0],
                     qmp_timer=15, args=qemu_arg_list[1:]) as vm:
        vm.launch()

        req_mt = get_req_mt(vm, args)
        qemu_drivers = DriverDefinitions(vm)
        comp_table = fill_prop_table(req_mt, qemu_drivers, args.raw)
        if not comp_table.empty:
            print_table(comp_table, args.format)

        vm.shutdown()
