"""
mkvenv - QEMU venv bootstrapping utility

usage: TODO/FIXME
"""

# Copyright (C) 2022-2023 Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import argparse
from importlib.util import find_spec
import logging
import os
from os import PathLike
from pathlib import Path
import re
import stat
import subprocess
import sys
import traceback
from types import SimpleNamespace
from typing import Generator, Dict, Sequence, Optional, Union, Iterator
import venv


# Do not add any mandatory dependencies from outside the stdlib:
# This script *must* be usable standalone!

DirType = Union[str, bytes, 'PathLike[str]', 'PathLike[bytes]']
logger = logging.getLogger('mkvenv')


class Ouch(RuntimeError):
    """An Exception class we can't confuse with a builtin."""


class QemuEnvBuilder(venv.EnvBuilder):
    """
    An extension of venv.EnvBuilder for building QEMU's configure-time venv.

    The only functional change is that it adds the ability to regenerate
    console_script shims for packages available via system_site
    packages.

    (And a metric ton of debugging printfs)

    Parameters for base class init:
      - system_site_packages: bool = False
      - clear: bool = False
      - symlinks: bool = False
      - upgrade: bool = False
      - with_pip: bool = False
      - prompt: Optional[str] = None
      - upgrade_deps: bool = False             (Since 3.9)
    """
    def __init__(self, *args, **kwargs) -> None:
        self.script_packages = kwargs.pop('script_packages', ())
        super().__init__(*args, **kwargs)
        # venv class is cute and toggles this off before post_setup,
        # but we need it to decide if we want to generate shims or not.
        self._system_site_packages = self.system_site_packages
        # Make the context available post-creation:
        self._context: Optional[SimpleNamespace] = None

    def ensure_directories(self, env_dir: DirType) -> SimpleNamespace:
        logger.debug("ensure_directories(env_dir=%s)", env_dir)
        context = super().ensure_directories(env_dir)
        # Here's what's in the context blob:
        logger.debug("env_dir      %s", context.env_dir)
        logger.debug("env_name     %s", context.env_name)
        logger.debug("prompt       %s", context.prompt)
        logger.debug("executable   %s", context.executable)
        logger.debug("inc_path     %s", context.inc_path)
        if 'lib_path' in context.__dict__:
            # 3.12+
            logger.debug("lib_path     %s", context.lib_path)
        logger.debug("bin_path     %s", context.bin_path)
        logger.debug("bin_name     %s", context.bin_name)
        logger.debug("env_exe      %s", context.env_exe)
        if 'env_exec_cmd' in context.__dict__:
            # 3.9+
            logger.debug("env_exec_cmd %s", context.env_exec_cmd)
        self._context = context
        return context

    def create(self, env_dir: DirType) -> None:
        logger.debug("create(env_dir=%s)", env_dir)
        super().create(env_dir)
        self.post_post_setup(self._context)

    def create_configuration(self, context: SimpleNamespace) -> None:
        logger.debug("create_configuration(...)")
        super().create_configuration(context)

    def setup_python(self, context: SimpleNamespace) -> None:
        logger.debug("setup_python(...)")
        super().setup_python(context)

    def setup_scripts(self, context: SimpleNamespace) -> None:
        logger.debug("setup_scripts(...)")
        super().setup_scripts(context)

    # def upgrade_dependencies(self, context): ...  # only Since 3.9

    def post_setup(self, context: SimpleNamespace) -> None:
        # Generate console_script entry points for system packages
        # e.g. meson, sphinx, pip, etc.
        logger.debug("post_setup(...)")
        if self._system_site_packages:
            generate_console_scripts(
                context.env_exe, context.bin_path, self.script_packages)
        #
        # print the python executable to stdout for configure.
        print(context.env_exe)

    def post_post_setup(self, context: SimpleNamespace) -> None:
        # This is the very final hook that occurs *after* enabling
        # system-site-packages.
        subprocess.run((context.env_exe, __file__, 'checkpip'), check=True)


def need_ensurepip() -> bool:
    """
    Tests for the presence of setuptools and pip.

    :return: `True` if we do not detect both packages.
    """
    # Don't try to actually import them, it's fraught with danger:
    # https://github.com/pypa/setuptools/issues/2993
    if find_spec("setuptools") and find_spec("pip"):
        return False
    return True


def make_venv(  # pylint: disable=too-many-arguments
        venv_path: Union[str, Path],
        system_site_packages: bool = False,
        clear: bool = True,
        symlinks: Optional[bool] = None,
        upgrade: bool = False,
        with_pip: Optional[bool] = None,
        script_packages: Sequence[str]=(),
) -> None:
    """
    Create a venv using the QemuEnvBuilder class.

    TODO: write docs O:-)
    """
    logging.debug("%s: make_venv(venv_path=%s, system_site_packages=%s, "
                  "clear=%s, upgrade=%s, with_pip=%s, script_packages=%s)",
                  __file__, str(venv_path), system_site_packages,
                  clear, upgrade, with_pip, script_packages)

    print(f"MKVENV {str(venv_path)}", file=sys.stderr)

    # ensurepip is slow: venv creation can be very fast for cases where
    # we allow the use of system_site_packages. Toggle ensure_pip on only
    # in the cases where we really need it.
    if with_pip is None:
        with_pip = True if not system_site_packages else need_ensurepip()
        logger.debug("with_pip unset, choosing %s", with_pip)

    if symlinks is None:
        # Default behavior of standard venv CLI
        symlinks = os.name != "nt"

    if with_pip and not find_spec("ensurepip"):
        msg = ("Python's ensurepip module is not found.\n"

               "It's normally part of the Python standard library, "
               "maybe your distribution packages it separately?\n"

               "Either install ensurepip, or alleviate the need for it in the "
               "first place by installing pip and setuptools for "
               f"'{sys.executable}'.\n"

               "(Hint: Debian puts ensurepip in its python3-venv package.)")
        raise Ouch(msg)

    builder = QemuEnvBuilder(
        system_site_packages=system_site_packages,
        clear=clear,
        symlinks=symlinks,
        upgrade=upgrade,
        with_pip=with_pip,
        script_packages=script_packages,
    )
    try:
        logger.debug("Invoking builder.create()")
        try:
            builder.create(str(venv_path))
        except SystemExit as exc:
            # Some versions of the venv module raise SystemExit; *nasty*!
            # We want the exception that prompted it. It might be a subprocess
            # error that has output we *really* want to see.
            logger.debug("Intercepted SystemExit from EnvBuilder.create()")
            raise exc.__cause__ or exc.__context__ or exc
        logger.debug("builder.create() finished")
    except subprocess.CalledProcessError as exc:
        print(f"cmd: {exc.cmd}", file=sys.stderr)
        print(f"returncode: {exc.returncode}", file=sys.stderr)
        if exc.stdout:
            print("========== stdout ==========", file=sys.stderr)
            print(exc.stdout, file=sys.stderr)
            print("============================", file=sys.stderr)
        if exc.stderr:
            print("========== stderr ==========", file=sys.stderr)
            print(exc.stderr, file=sys.stderr)
            print("============================", file=sys.stderr)
        raise Ouch("VENV creation subprocess failed.") from exc


def _gen_importlib(packages: Sequence[str]) -> Iterator[Dict[str, str]]:
    # pylint: disable=import-outside-toplevel
    try:
        # First preference: Python 3.8+ stdlib
        from importlib.metadata import (
            PackageNotFoundError,
            distribution,
        )
    except ImportError as exc:
        logger.debug("%s", str(exc))
        # Second preference: Commonly available PyPI backport
        from importlib_metadata import (
            PackageNotFoundError,
            distribution,
        )

    # Borrowed from CPython (Lib/importlib/metadata/__init__.py)
    pattern = re.compile(
        r'(?P<module>[\w.]+)\s*'
        r'(:\s*(?P<attr>[\w.]+)\s*)?'
        r'((?P<extras>\[.*\])\s*)?$'
    )

    def _generator() -> Iterator[Dict[str, str]]:
        for package in packages:
            try:
                entry_points = distribution(package).entry_points
            except PackageNotFoundError:
                continue

            # The EntryPoints type is only available in 3.10+,
            # treat this as a vanilla list and filter it ourselves.
            entry_points = filter(
                lambda ep: ep.group == 'console_scripts', entry_points)

            for entry_point in entry_points:
                # Python 3.8 doesn't have 'module' or 'attr' attributes
                if not (hasattr(entry_point, 'module') and
                        hasattr(entry_point, 'attr')):
                    match = pattern.match(entry_point.value)
                    assert match is not None
                    module = match.group('module')
                    attr = match.group('attr')
                else:
                    module = entry_point.module
                    attr = entry_point.attr
                yield {
                    'name': entry_point.name,
                    'module': module,
                    'import_name': attr,
                    'func': attr,
                }

    return _generator()


def _gen_pkg_resources(packages: Sequence[str]) -> Iterator[Dict[str, str]]:
    # pylint: disable=import-outside-toplevel
    # Bundled with setuptools; has a good chance of being available.
    import pkg_resources

    def _generator() -> Iterator[Dict[str, str]]:
        for package in packages:
            try:
                eps = pkg_resources.get_entry_map(package, 'console_scripts')
            except pkg_resources.DistributionNotFound:
                continue

            for entry_point in eps.values():
                yield {
                    'name': entry_point.name,
                    'module': entry_point.module_name,
                    'import_name': ".".join(entry_point.attrs),
                    'func': ".".join(entry_point.attrs),
                }

    return _generator()


# Borrowed/adapted from pip's vendored version of distutils:
SCRIPT_TEMPLATE = r'''#!{python_path:s}
# -*- coding: utf-8 -*-
import re
import sys
from {module:s} import {import_name:s}
if __name__ == '__main__':
    sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])
    sys.exit({func:s}())
'''


def generate_console_scripts(python_path: str, bin_path: str,
                             packages: Sequence[str]) -> None:
    """
    Generate script shims for console_script entry points in @packages.
    """
    if not packages:
        return

    def _get_entry_points() -> Iterator[Dict[str, str]]:
        try:
            return _gen_importlib(packages)
        except ImportError as exc:
            logger.debug("%s", str(exc))
        return _gen_pkg_resources(packages)

    try:
        entry_points = _get_entry_points()
    except ImportError as exc:
        logger.debug("%s", str(exc))
        raise Ouch(
            "Neither importlib.metadata nor pkg_resources found, "
            "can't generate console script shims.\n"
            "Use Python 3.8+, or install importlib-metadata, or setuptools."
        ) from exc

    for entry_point in entry_points:
        script_path = os.path.join(bin_path, entry_point['name'])
        script = SCRIPT_TEMPLATE.format(python_path=python_path, **entry_point)
        with open(script_path, "w", encoding='UTF-8') as file:
            file.write(script)
            fd = file.fileno()
            os.chmod(fd, os.stat(fd).st_mode | stat.S_IEXEC)
        logger.debug("wrote '%s'", script_path)


def checkpip():
    """
    Debian10 has a pip that's broken when used inside of a virtual environment.

    We try to detect and correct that case here.
    """
    try:
        import pip._internal
        logger.debug("pip appears to be working correctly.")
        return
    except ModuleNotFoundError as exc:
        if exc.name == 'pip._internal':
            # Uh, fair enough. They did say "internal".
            # Let's just assume it's fine.
            return
        logger.warning("pip appears to be malfunctioning: %s", str(exc))

    # Test for ensurepip:
    try:
        import ensurepip
    except ImportError as exc:
        raise Ouch(
            "pip appears to be non-functional, "
            "and Python's ensurepip module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "(Hint: Debian puts ensurepip in its python3-venv package.)"
        ) from exc

    logging.debug("Attempting to repair pip ...")
    subprocess.run((sys.executable, '-m', 'ensurepip'),
                   stdout=subprocess.DEVNULL, check=True)
    logging.debug("Pip is now (hopefully) repaired!")


def main() -> int:
    """CLI interface to make_qemu_venv. See module docstring."""
    if os.environ.get('DEBUG') or os.environ.get('GITLAB_CI'):
        # You're welcome.
        logging.basicConfig(level=logging.DEBUG)

    parser = argparse.ArgumentParser(description="Bootstrap QEMU venv.")
    subparsers = parser.add_subparsers(
        title="Commands",
        description="Various actions this utility can perform",
        prog="prog",
        dest="command",
        required=True,
        metavar="command",
        help='Description')

    #
    subparser = subparsers.add_parser('create', help='create a venv')
    subparser.add_argument(
        '--gen',
        type=str,
        action='append',
        help="Regenerate console_scripts for given packages, if found.",
    )
    subparser.add_argument(
        'target',
        type=str,
        action='store',
        help="Target directory to install virtual environment into.",
    )

    #
    subparser = subparsers.add_parser(
        'checkpip', help='test pip and fix if necessary')

    args = parser.parse_args()

    try:
        if args.command == 'create':
            script_packages = []
            for element in args.gen or ():
                script_packages.extend(element.split(","))
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
                upgrade=False,
                with_pip=None,  # Autodetermine
                script_packages=script_packages,
            )
            logger.debug("mkvenv.py create - exiting")
        if args.command == 'checkpip':
            checkpip()
            logger.debug("mkvenv.py checkpip - exiting")
    except Ouch as exc:
        print("\n*** Ouch! ***\n", file=sys.stderr)
        print(str(exc), "\n\n", file=sys.stderr)
        return 1
    except:  # pylint: disable=bare-except
        print("mkvenv did not complete successfully:", file=sys.stderr)
        traceback.print_exc()
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
