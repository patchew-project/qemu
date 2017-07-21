#!/usr/bin/env python
#
# QEMU build configuration introspection utilty
#
# Copyright (C) 2017 Red Hat Inc.
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
# Authors:
#  Cleber Rosa <crosa@redhat.com>

"""
QEMU's build configuration is recorded in both config-host.mak and
config-host.h, as is specific target configuration expressed in
<target>/config-{target,devices}.{mak,h}.

This module relies on the .mak files, as they contain a bit more
information than the .h files.

While it would be possible to write a simple Makefile parser capable to
handle the variable assignments, this would impose limitations on this
script and introduce breakages if the build scripts start using
functionality not expected here.

The approach chosen was one that is definitely slower at runtime but
is more reliable.  Temporary Makefiles that include config-host.mak,
and optionally the target specific config-target.mak and
config-devices.mak files, and print the desired configuration to
stdout.  As long as the basic premises of a global config-host.mak,
and target specific config-target.mak and config-devices.mak is kept,
this tool should be able to keep up with any style or feature chances.
"""

from __future__ import print_function

import optparse
import os
import subprocess
import sys
import tempfile


TEMPLATE = """
include {build_prefix}/config-host.mak
{target_specific}

all:
	@echo $({conf})
"""

TARGET_TEMPLATE = """
include {build_prefix}/{target}/config-target.mak
include {build_prefix}/{target}/config-devices.mak
"""


class InvalidTarget(Exception):
    """
    Target chosen is not present in the current build tree configuration
    """


def get_build_root():
    """
    Returns the absolute location of the root of the build tree

    This has been tested from build(only) trees, and works fine when
    it's executed as a command line tool.

    If this is used as a Python module, it will really depend on how
    the module is imported.  If the build tree "scripts" directory is
    added to the import path, this will work properly.  If the current
    working directory is the "scripts" directory itself, and no
    explicit import path is added, it will only work when building
    from the source tree, and will *not* work when the build tree is
    different from the source tree.

    A longer explanation on this caveat: the Python import
    implementation will look for a matching module in the current
    working directory.  Python's current working directory,
    os.getcwd(), is really like getcwd(3), and not like
    os.getenv("PWD").  Because the scripts directory is linked to the
    source tree, os.getcwd() returns the source tree location instead.
    """
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def is_build_root_configured():
    """
    Checks if the build root has been configured

    In theory, this only makes sense for in-tree builds, because the
    out-of-tree build directory, including the link to the scripts
    directory containing this script, will only exist after a
    successful "./configure" execution.

    Either way, the check is still valid for the main source of
    build configuration, that is, the existence of 'config-host.mak'
    """
    return os.path.isfile(os.path.join(get_build_root(), 'config-host.mak'))


def get_default_target():
    """
    Returns the default target on the current build tree

    The approach used here is to look for a "-softmmu" target that
    matches "ARCH".  If found, that is the best choice for a default
    target.

    If not found, the first "-softmmu" target found is considered the
    next best choice for a default target.

    As a fallback if no "-softmmu" target exists, the first entry on
    the target list is returned.

    :returns: a target name or None if no target is configured
    """
    targets = get_targets()
    if not targets:
        return None
    arch = get_build_conf("ARCH", None)
    first_choice = "%s-softmmu" % arch
    if first_choice in targets:
        return first_choice
    else:
        softmmu_targets = [t for t in targets if t.endswith("-softmmu")]
        if softmmu_targets:
            return softmmu_targets[0]
        else:
            return targets[0]


def get_build_conf(conf, target=None):
    """
    Returns the value of a given Makefile variable

    :param conf: the configuration name, which really must be a
                 Makefile variable in either the host or target .mak
                 files
    :param target: the name of a valid target in the current build tree.
                   it must match the name of a target dir, such as
                   'x86_64-softmmu' or 'i386-linux-user'.
    :returns: the raw output or None
    :rtype: str or None
    """
    build_prefix = get_build_root()

    if target is None:
        target_specific = ''
    else:
        if target not in get_targets():
            raise InvalidTarget
        target_specific = TARGET_TEMPLATE.format(build_prefix=build_prefix,
                                                 target=target)

    mak_fd, mak_path = tempfile.mkstemp()
    os.write(mak_fd, TEMPLATE.format(build_prefix=build_prefix,
                                     target_specific=target_specific,
                                     conf=conf))
    proc = subprocess.Popen(['make', '-f', mak_path],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    ret = proc.wait()
    os.unlink(mak_path)
    if (ret == 0):
        return proc.stdout.read().strip()


def is_enabled(conf, target=None):
    """
    Checks wether a given feature is enabled in the build configuration

    The Makefile variables default to using 'y' when they are enabled,
    and are just missing (instead of set no 'n') when they're not enabled.
    Even if a given variable is set, for instance in the case of TARGET_DIRS,
    it will not be considered enabled by this function unless its content is
    'y'.  For variables that are known to not contain 'y', please resort to
    using get_build_conf() and parsing its output for meaningful value.

    :param conf: the configuration name, which really must be a
                 Makefile variable in either the host or target .mak
                 files
    :param target: the name of a valid target in the current build tree.
                   it must match the name of a target dir, such as
                   'x86_64-softmmu' or 'i386-linux-user'.
    :returns: the raw output or None
    :rtype: str or None
    """
    build_conf = get_build_conf(conf, target=target)
    if build_conf == 'y':
        return True
    return False


def get_targets():
    """
    Returns the list of targets currently configured

    :rtype: list
    """
    targets = get_build_conf('TARGET_DIRS', None)
    if targets is not None:
        return targets.split()


if __name__ == '__main__':
    class Parser(optparse.OptionParser):

        def __init__(self):
            optparse.OptionParser.__init__(
                self,
                usage=('%prog [options] CONFIG [TARGET]\n\n'
                       'CONFIG is the build configuration variable name\n'
                       'TARGET is auto selected if not explicitly set'))
            self.add_option('-c', '--check', action='store_true',
                            help=('Checks if the build configuration option is '
                                  'set to "y".  This causes this tool to be silent '
                                  'and return only a status code of either 0 (if '
                                  'configuration is set) or non-zero otherwise.'))
            self.add_option('-n', '--no-default-target', action='store_true',
                            help=('Do not attempt to use a default target if one '
                                  'was not explicitly given in the command line'))
            self.add_option('--print-target', action='store_true',
                            help=('Also prints the selected target'))


    class App(object):

        def __init__(self):
            self.target = None
            self.parser = Parser()
            self._parse()

        def _parse(self):
            self.opts, self.args = self.parser.parse_args()
            args_len = len(self.args)
            if (args_len < 1 or args_len > 2):
                self.parser.print_help()
                sys.exit(0)
            elif args_len == 2:
                self.target = self.args[1]
            else:
                if not self.opts.no_default_target:
                    self.target = get_default_target()

        def run(self):
            if self.opts.print_target:
                print("TARGET:", self.target)
            config = self.args[0]
            if self.opts.check:
                result = is_enabled(config, self.target)
                if result:
                    sys.exit(0)
                else:
                    sys.exit(-1)
            else:
                conf = get_build_conf(config, self.target)
                if conf:
                    print(conf)
                    sys.exit(0)
                else:
                    sys.exit(-1)


    app = App()
    app.run()
