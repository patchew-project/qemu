#!/usr/bin/env python3
#
# Copyright (c) Yandex Technologies LLC, 2022
#
# Script to compare machine type compatible properties
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
import json
import sys
from os import path
from argparse import ArgumentParser, RawTextHelpFormatter
import pandas as pd

try:
    qemu_dir = path.abspath(path.dirname(path.dirname(__file__)))
    sys.path.append(path.join(qemu_dir, 'python'))
    from qemu.machine import QEMUMachine
    from qemu.qmp import ConnectError
except ModuleNotFoundError as exc:
    print(f"Module '{exc.name}' not found.")
    print("Try export PYTHONPATH=top-qemu-dir/python or run from top-qemu-dir")
    sys.exit(1)


DEF_CMD_LINE = 'build/qemu-system-x86_64 -enable-kvm -machine none'


# Methods to get right values of drivers props
#
# Use these methods as a 'whitelist' and add entries only if necessary. It's
# important in analysis and tests.
# Names should be in qom-list-types format (486-x86_64-cpu, not 486)
# The latest definition wins:
# 1) device
# 2) cpu
# 3) 486-x86_64-cpu
# For 486 will be used - 3) rule, for virtio device - 1), for Haswell - 2)
def get_device_prop(vm, device, prop_name):
    args = {'typename': device}
    device_props = vm.command('device-list-properties', **args)
    for prop in device_props:
        if prop['name'] == prop_name:
            return str(prop.get('default-value', 'No default value'))

    return 'Unknown property'


def get_x86_64_cpu_prop(vm, device, prop_name):
    # crop last 11 chars '-x86_64-cpu'
    args = {'type': 'full', 'model': {'name': device[:-11]}}
    props = vm.command('query-cpu-model-expansion', **args)['model']['props']
    return str(props.get(prop_name, 'Unknown property'))


# Now it's stub, because all memory_backend types don't have default values
# but this behaviour can be changed
def get_memory_backend_prop(vm, driver, prop_name):
    args = {'typename': driver}
    memory_backend_props = vm.command('qom-list-properties', **args)
    for prop in memory_backend_props:
        if prop['name'] == prop_name:
            return str(prop.get('default-value', 'No default value'))

    return 'Unknown property'


property_methods = [
    {'name': 'device', 'method': get_device_prop},
    {'name': 'x86_64-cpu', 'method': get_x86_64_cpu_prop},
    {'name': 'memory-backend', 'method': get_memory_backend_prop}
    ]
# End of methods definition


def parse_args():
    parser = ArgumentParser(formatter_class=RawTextHelpFormatter,
                            description='Script to compare machine types '
                            '(their compat_props).\n\n'
                            'The script execution with --all option may take '
                            'several minutes!\n\n'
                            'If a property applies to an abstract class this '
                            'script collects default values of all child '
                            'classes and prints them as a set.\n\n'
                            '"Unavailable method" - means that this script '
                            'doesn\'t know how to get default values of the '
                            'driver. To add method use the construction '
                            'described at the top of the script.\n'
                            '"Unavailable driver" - means that this script '
                            'doesn\'t know this driver. For instance, this '
                            'can happen if you configure QEMU without this '
                            'device or if machine type definition has error.\n'
                            '"No default value" - means that the appropriate '
                            'method can\'t get the default value and most '
                            'likely that this property doesn\'t have it.\n'
                            '"Unknown property" - means that the appropriate '
                            'method can\'t find property with this name.')

    parser.add_argument('--json', action='store_true',
                        help='returns table in json format')
    parser.add_argument('--csv', action='store_true',
                        help='returns table in csv format')
    parser.add_argument('--all', action='store_true',
                        help='prints all available machine types (list of '
                             'machine types will be ignored)')
    parser.add_argument('--MT', nargs="*", type=str,
                        help='list of Machine Types that will be compared')
    parser.add_argument('--raw', action='store_true',
                        help='prints ALL defined properties without '
                             'value transformation. By default, '
                             'only properties with different values '
                             'printed and with value transformation(like '
                             '"on" -> True)')
    parser.add_argument('--cmd-line', default=DEF_CMD_LINE,
                        help='command line to start qemu.'
                             'Default: {}'.format(DEF_CMD_LINE))
    return parser.parse_args()


# return touple (name, major, minor, revision)
def MT_comp(MT):
    splited_name = MT['name'].rsplit('-', 1)
    if len(splited_name) == 2:
        version = splited_name[1].split('.')
        if len(version) == 2:
            return (splited_name[0], int(version[0]), int(version[1]), 0)
        if len(version) == 3:
            return (splited_name[0],
                    int(version[0]), int(version[1]), int(version[2]))

    return (splited_name[0], 0, 0, 0)


def get_MT_definitions(vm):
    args = {'compat-props': True}
    raw_MT_defs = vm.command('query-machines', **args)
    MT_defs = [] # MT: {driver_1: set_of_props, ...}
    for raw_MT in raw_MT_defs:
        compat_props = {}
        for prop in raw_MT['compat-props']:
            if not compat_props.get(prop['driver'], None):
                compat_props[prop['driver']] = {}
            compat_props[prop['driver']][prop['property']] = prop['value']
        MT_defs.append({'name': raw_MT['name'], 'compat-props': compat_props})

    MT_defs.sort(key=MT_comp)
    return MT_defs


def get_req_props(MT_defs):
    driver_props = {}
    for MT in MT_defs:
        compat_props = MT['compat-props']
        for driver, prop in compat_props.items():
            if driver not in driver_props:
                driver_props[driver] = set()
            driver_props[driver].update(prop.keys())

    return driver_props


def get_driver_definitions(vm):
    args = {'abstract': True}
    qom_all_types = vm.command('qom-list-types', **args)

    driver_to_def = {}
    for obj_type in qom_all_types:
        # parent of Object is None
        parent = obj_type.get('parent', None)
        abstr  = obj_type.get('abstract', False)
        driver_to_def[obj_type['name']] = {
            'parent': parent,
            'abstract': abstr,
            'method': None}
        if abstr:
            type_args = {'implements': obj_type['name'], 'abstract': True}
            list_child_objs = vm.command('qom-list-types', **type_args)
            child_list = [child['name'] for child in list_child_objs]
            driver_to_def[obj_type['name']]['child_list'] = child_list

    for driver in property_methods:
        if not driver_to_def[driver['name']]['abstract']:
            driver_to_def[driver['name']]['method'] = driver['method']
            continue

        for child in driver_to_def[driver['name']]['child_list']:
            driver_to_def[child]['method'] = driver['method']

    return driver_to_def


def fill_prop_table(vm, MT_list, driver_props, driver_defs):
    table = {}
    for driver, props in sorted(driver_props.items()):
        for prop in sorted(props):
            name = '{}-{}'.format(driver, prop)
            table[name] = []
            for MT in MT_list:
                compat_props = MT['compat-props']
                if compat_props.get(driver, None):
                    if compat_props[driver].get(prop, None):
                        table[name].append(compat_props[driver][prop])
                        continue

                # properties from QEMU (not from machine type compat_props)
                # properties from another architecture or config
                if not driver_defs.get(driver, None):
                    table[name].append('Unavailable driver')
                    continue

                if not driver_defs[driver]['abstract']:
                    if driver_defs[driver]['method'] is None:
                        table[name].append('Unavailable method')
                    else:
                        table[name].append(
                            driver_defs[driver]['method'](vm, driver, prop))
                else:
                    children = driver_defs[driver]['child_list']
                    values = set()
                    for child in children:
                        if driver_defs[child]['abstract']:
                            continue

                        if driver_defs[child]['method'] is None:
                            values.add('Unavailable method')
                        else:
                            values.add(
                                driver_defs[child]['method'](vm, child, prop))

                    table[name].append(list(values))

    headers = [MT['name'] for MT in MT_list]
    return pd.DataFrame.from_dict(table, orient='index', columns=headers)


def transform_value(value):
    true_list = ['true', 'on']
    false_list = ['false', 'off']

    out = value.lower()

    if out in true_list:
        return True

    if out in false_list:
        return False

    return out


# Only hex, dec and oct formats
def transform_number(value):
    try:
        # C doesn't work with underscore ('2_5' != 25)
        if '_' in value:
            raise ValueError

        if 'x' in value or 'X' in value:
            return int(value, 16)

        if 'o' in value or 'O' in value:
            return int(value, 8)

        return int(value)

    except ValueError:
        return None


def transformed_table(table):
    new_table = {}
    for index, row in table.iterrows():
        new_row = []
        all_values = set()
        # We want to save original hex/decimal format if not all values
        # are the same in the row. So, transformed and not transformed will be
        # stored
        numeric_values = set()
        for MT_prop_val in row:
            if type(MT_prop_val) is list:
                transformed = [transform_value(val) for val in MT_prop_val]
                if len(transformed) == 1:
                    new_row.append(transformed[0])
                else:
                    new_row.append(transformed)

                numeric_values.update(set([transform_number(val)
                                           for val in MT_prop_val]))
                all_values.update(set(transformed))
            else:
                transformed = transform_value(MT_prop_val)
                new_row.append(transformed)
                numeric_values.add(transform_number(MT_prop_val))
                all_values.add(transformed)

        if len(table.columns) > 1:
            if len(all_values) == 1:
                continue

            if not None in numeric_values and len(numeric_values) == 1:
                continue

        new_table[index] = new_row

    return pd.DataFrame.from_dict(new_table, orient='index',
                                  columns=table.columns.values)


if __name__ == '__main__':
    args = parse_args()
    qemu_arg_list = args.cmd_line.split(' ')
    with QEMUMachine(binary=qemu_arg_list[0],
                     qmp_timer=15, args=qemu_arg_list[1:]) as vm:
        vm.launch()
        MT_defs = get_MT_definitions(vm)
        list_MT = [MT['name'] for MT in MT_defs]

        if not args.all:
            if args.MT is None:
                print('Enter machine types for comparision or use --help')
                print('List of available machine types:')
                print(*list_MT, sep='\n')
                sys.exit(1)

            for MT in args.MT:
                if MT not in list_MT:
                    print('Wrong machine type name')
                    print('List of available machine types:')
                    print(*list_MT, sep='\n')
                    sys.exit(1)

        req_MT = []
        if args.all:
            req_MT = MT_defs
        else:
            for MT in MT_defs:
                if MT['name'] in args.MT:
                    req_MT.append(MT)

        if len(req_MT) == 1:
            args.full = True

        req_driver_props = get_req_props(req_MT)
        driver_defs = get_driver_definitions(vm)
        comp_table = fill_prop_table(vm, req_MT, req_driver_props, driver_defs)
        if not args.raw:
            comp_table = transformed_table(comp_table)

        if args.json:
            print(comp_table.to_json())
        elif args.csv:
            print(comp_table.to_csv())
        else:
            print(tabulate(comp_table, showindex=True, stralign='center',
                           tablefmt='fancy_grid', headers='keys',
                           disable_numparse=True))

        vm.shutdown()
