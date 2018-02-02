#!/usr/bin/env python
# QAPI generator
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

import getopt
import re
import sys
from qapi.common import QAPIError, QAPISchema
from qapi.types import gen_types
from qapi.visit import gen_visit
from qapi.commands import gen_commands
from qapi.events import gen_events
from qapi.introspect import gen_introspect
from qapi.doc import gen_doc


def parse_command_line(extra_options='', extra_long_options=[]):

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:],
                                       'chp:o:' + extra_options,
                                       ['source', 'header', 'prefix=',
                                        'output-dir='] + extra_long_options)
    except getopt.GetoptError as err:
        print >>sys.stderr, "%s: %s" % (sys.argv[0], str(err))
        sys.exit(1)

    output_dir = ''
    prefix = ''
    do_c = False
    do_h = False
    extra_opts = []

    for oa in opts:
        o, a = oa
        if o in ('-p', '--prefix'):
            match = re.match(r'([A-Za-z_.-][A-Za-z0-9_.-]*)?', a)
            if match.end() != len(a):
                print >>sys.stderr, \
                    "%s: 'funny character '%s' in argument of --prefix" \
                    % (sys.argv[0], a[match.end()])
                sys.exit(1)
            prefix = a
        elif o in ('-o', '--output-dir'):
            output_dir = a + '/'
        elif o in ('-c', '--source'):
            do_c = True
        elif o in ('-h', '--header'):
            do_h = True
        else:
            extra_opts.append(oa)

    if not do_c and not do_h:
        do_c = True
        do_h = True

    if len(args) != 1:
        print >>sys.stderr, "%s: need exactly one argument" % sys.argv[0]
        sys.exit(1)
    fname = args[0]

    return (fname, output_dir, do_c, do_h, prefix, extra_opts)


def main(argv):
    (input_file, output_dir, do_c, do_h, prefix, opts) = \
        parse_command_line('bu', ['builtins', 'unmask-non-abi-names'])

    opt_builtins = False
    opt_unmask = False

    for o, a in opts:
        if o in ('-b', '--builtins'):
            opt_builtins = True
        if o in ('-u', '--unmask-non-abi-names'):
            opt_unmask = True

    try:
        schema = QAPISchema(input_file)
    except QAPIError as err:
        print >>sys.stderr, err
        exit(1)

    gen_types(schema, output_dir, prefix, opt_builtins)
    gen_visit(schema, output_dir, prefix, opt_builtins)
    gen_commands(schema, output_dir, prefix)
    gen_events(schema, output_dir, prefix)
    gen_introspect(schema, output_dir, prefix, opt_unmask)
    gen_doc(schema, output_dir, prefix)


if __name__ == '__main__':
    main(sys.argv)
