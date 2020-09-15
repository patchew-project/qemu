#
# QAPI helper library
#
# Copyright IBM, Corp. 2011
# Copyright (c) 2013-2018 Red Hat Inc.
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.
# See the COPYING file in the top-level directory.

from typing import Optional

from .common import c_name
from .schema import QAPISchemaObjectType


def build_params(arg_type: Optional[QAPISchemaObjectType],
                 boxed: bool,
                 extra: Optional[str] = None) -> str:
    ret = ''
    sep = ''
    if boxed:
        assert arg_type
        ret += '%s arg' % arg_type.c_param_type()
        sep = ', '
    elif arg_type:
        assert not arg_type.variants
        for memb in arg_type.members:
            ret += sep
            sep = ', '
            if memb.optional:
                ret += 'bool has_%s, ' % c_name(memb.name)
            ret += '%s %s' % (memb.type.c_param_type(),
                              c_name(memb.name))
    if extra:
        ret += sep + extra
    return ret if ret else 'void'
