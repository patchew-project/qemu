#! /usr/bin/env python3

# Parse configure command line options based on Meson's user build options
# introspection data (passed on stdin).
#
# Copyright (C) 2020 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

import json
import re
import shlex
import sys
import textwrap

SKIP_OPTIONS = [ 'docdir', 'qemu_suffix' ]
FEATURE_CHOICES = [ 'auto', 'disabled', 'enabled' ]

options = { x['name']: x
            for x in json.load(sys.stdin)
            if x['section'] == 'user' and x['name'] not in SKIP_OPTIONS }

for x in options.values():
    if x['type'] == 'combo' and sorted(x['choices']) == FEATURE_CHOICES:
        x['type'] = 'feature'

def value_to_help(x):
    if x == True:
        return 'enabled'
    if x == False:
        return 'disabled'
    return str(x)

def print_help_line(key, opt, indent):
    key = '  ' + key
    value = '%s [%s]' % (opt['description'], value_to_help(opt['value']))
    if len(key) >= indent:
        print(key)
        key = ''
    spaces = ' ' * indent
    key = (key + spaces)[0:indent]
    print(textwrap.fill(value, initial_indent=key, subsequent_indent=spaces))

def print_help():
    for o, x in options.items():
        if x['type'] not in ('boolean', 'feature'):
            print_help_line('--enable-' + o, x, 24)

    print()
    print('Optional features, enabled with --enable-FEATURE and')
    print('disabled with --disable-FEATURE:')
    for o, x in options.items():
        if x['type'] in ('boolean', 'feature'):
            print_help_line(o, x, 18)

def error(s, *args):
    print('ERROR:', s % args, file=sys.stderr)
    sys.exit(1)

def main(argv):
    if not argv:
        return

    if argv[0] == '--print-help':
        print_help()
        return

    args = []
    for arg in sys.argv[1:]:
        m = re.search('--(enable|disable)-([^=]*)(?:=(.*))?', arg)
        if not m:
            error('internal error parsing command line')
        opt = m.group(2).replace('-', '_')
        if opt not in options:
            error('Unknown option --%s-%s', m.group(1), m.group(2))
        opt_type = options[opt]['type']
        if opt_type in ('boolean', 'feature'):
            if m.group(3) is not None:
                error('option --%s-%s does not take an argument', m.group(1), m.group(2))
            if opt_type == 'feature':
                value = m.group(1) + 'd'
            else:
                value = 'true' if m.group(1) == 'enable' else 'false'
        else:
            if m.group(1) == 'disable':
                error('Unknown option --disable-%s', m.group(2))
            if m.group(3) is None:
                error('option --enable-%s takes an argument', m.group(2))

        args.append(shlex.quote('-D%s=%s' % (opt, value)))
    print(' '.join(args))

if __name__ == "__main__":
    main(sys.argv[1:])
