#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Plugin backend.
"""

__author__     = "Alex Bennée <alex.bennee@linaro.org>"
__copyright__  = "Copyright 2018, Alex Bennée <alex.bennee@linaro.org>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Alex Bennée"
__email__      = "alex.bennee@linaro.org"


from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    for event in events:
        # prototype for plugin event
        out('bool _plugin_%(api)s(%(args)s);',
            api=event.api(),
            args=event.args)
        # prototype for plugin fn
        out("typedef bool (* _plugin_%(api)s_fn)(%(args)s);",
            api=event.api(),
            args=event.args)


def generate_h(event, group):
    out('    if (!_plugin_%(api)s(%(args)s)) {',
        '        return;',
        '    };',
        api=event.api(),
        args=", ".join(event.args.names()))

def generate_c_begin(events, group):
    out('#include "qemu/osdep.h"',
        '#include "trace/control.h"',
        '')

def generate_c(event, group):
    out('bool _plugin_%(api)s(%(args)s)',
        '{',
        api=event.api(),
        args=event.args)

    event_id = 'TRACE_' + event.name.upper()
    cond = "trace_event_get_state(%s)" % event_id

    # Forst the pre-amble, bail early if the event is not enabled and
    # if it is but no plugin is enabled let the reset of the events proceed.

    out('',
        '    if (!%(cond)s) {',
        '        return false;',
        '    }',
        '',
        '    uintptr_t fp = trace_event_get_plugin(&_%(event)s_EVENT);',
        '    if (!fp) {',
        '        return true;',
        '    }',
        '',
        cond=cond,
        event=event_id)

    # We need to construct cast to the correct fn pointer to now call the plugin

    out('',
        '    _plugin_%(api)s_fn plug_fn = (_plugin_%(api)s_fn) fp;',
        '    return plug_fn(%(names)s);',
        '}',
        '',
        api=event.api(),
        names=", ".join(event.args.names()))
