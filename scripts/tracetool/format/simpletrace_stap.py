#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Generate .stp file that outputs simpletrace binary traces (DTrace with SystemTAP only).
"""

__author__     = "Stefan Hajnoczi <redhat.com>"
__copyright__  = "Copyright (C) 2014, Red Hat, Inc."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out
from tracetool.backend.dtrace import binary, probeprefix
from tracetool.backend.simple import is_string
from tracetool.format.stap import stap_escape

def global_var_name(name):
    return probeprefix().replace(".", "_") + "_" + name

def generate(events, backend, group):
    id_map = global_var_name("id_map")

    out('/* This file is autogenerated by tracetool, do not edit. */',
        '',
        'global %(id_map)s',
        '',
        'probe begin',
        '{',
        '    printf("%%8b%%8b%%8b", 0xffffffffffffffff, 0xf2b177cb0aa429b4, 4)',
        '}',
        '',
        id_map=id_map)

    for event_id, e in enumerate(events):
        if 'disable' in e.properties:
            continue

        out('probe %(probeprefix)s.simpletrace.%(name)s = %(probeprefix)s.%(name)s ?',
            '{',
            probeprefix=probeprefix(),
            name=e.name)

        out('    if (!(["%(name)s"] in %(id_map)s)) {',
            '        %(id_map)s["%(name)s"] = 1',
            '        printf("%%8b%%8b%%4b%%.*s", 0, ',
            '               %(event_id)s, %(name_len)s, %(name_len)s,',
            '               "%(name)s")',
            '    }',
            id_map=id_map,
            name=e.name,
            name_len=len(e.name),
            event_id=event_id)

        # Calculate record size
        sizes = ['24'] # sizeof(TraceRecord)
        for type_, name in e.args:
            name = stap_escape(name)
            if is_string(type_):
                out('    try {',
                    '        arg%(name)s_str = %(name)s ? user_string_n(%(name)s, 512) : "<null>"',
                    '    } catch {}',
                    '    arg%(name)s_len = strlen(arg%(name)s_str)',
                    name=name)
                sizes.append('4 + arg%s_len' % name)
            else:
                sizes.append('8')
        sizestr = ' + '.join(sizes)

        # Generate format string and value pairs for record header and arguments
        fields = [('8b', str(event_id)),
                  ('8b', 'gettimeofday_ns()'),
                  ('4b', sizestr),
                  ('4b', 'pid()')]
        for type_, name in e.args:
            name = stap_escape(name)
            if is_string(type_):
                fields.extend([('4b', 'arg%s_len' % name),
                               ('.*s', 'arg%s_len, arg%s_str' % (name, name))])
            else:
                fields.append(('8b', name))

        # Emit the entire record in a single SystemTap printf()
        fmt_str = '%'.join(fmt for fmt, _ in fields)
        arg_str = ', '.join(arg for _, arg in fields)
        out('    printf("%%8b%%%(fmt_str)s", 1, %(arg_str)s)',
            fmt_str=fmt_str, arg_str=arg_str)

        out('}')

    out()
