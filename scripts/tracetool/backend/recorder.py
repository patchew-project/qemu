# -*- coding: utf-8 -*-

"""
Trace back-end for recorder library
"""

__author__     = "Christophe de Dinechin <christophe@dinechin.org>"
__copyright__  = "Copyright 2020, Christophe de Dinechin and Red Hat"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Christophe de Dinechin"
__email__      = "christophe@dinechin.org"


from tracetool import out

PUBLIC = True

def generate_h_begin(events, group):
    out('#include <recorder/recorder.h>', '')

    for event in events:
        out('RECORDER_DECLARE(%(name)s);', name=event.name)


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    out('    record(%(event)s, %(fmt)s %(argnames)s);',
        event=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    RECORDER_TWEAK(%(event_id)s) || \\', event_id=event.name)

def generate_c_begin(events, group):
    out('#include "qemu/osdep.h"',
        '#include "trace/control.h"',
        '#include "trace/simple.h"',
        '#include <recorder/recorder.h>',
        '')

    for event in events:
        out('RECORDER_DEFINE(%(name)s, 8, "Tracetool recorder for %(api)s(%(args)s)");',
            name=event.name,
            api=event.api(),
            args=event.args)
