"""
mkvenv - QEMU pyvenv bootstrapping utility

usage: mkvenv [-h] command ...

QEMU pyvenv bootstrapping utility

options:
  -h, --help  show this help message and exit

Commands:
  command     Description
    create    create a venv

--------------------------------------------------

usage: mkvenv create [-h] [--gen GEN] target

positional arguments:
  target      Target directory to install virtual environment into.

options:
  -h, --help  show this help message and exit
  --gen GEN   Regenerate console_scripts for given packages, if found.

"""

# Copyright (C) 2022-2023 Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#  Paolo Bonzini <pbonzini@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import argparse
from importlib.util import find_spec
import logging
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import traceback
from types import SimpleNamespace
from typing import (
    Any,
    Dict,
    Iterator,
    Optional,
    Sequence,
    Union,
)
import venv


# Do not add any mandatory dependencies from outside the stdlib:
# This script *must* be usable standalone!

logger = logging.getLogger("mkvenv")


class Ouch(RuntimeError):
    """An Exception class we can't confuse with a builtin."""


class QemuEnvBuilder(venv.EnvBuilder):
    """
    An extension of venv.EnvBuilder for building QEMU's configure-time venv.

    The only functional change is that it adds the ability to regenerate
    console_script shims for packages available via system_site
    packages.

    Parameters for base class init:
      - system_site_packages: bool = False
      - clear: bool = False
      - symlinks: bool = False
      - upgrade: bool = False
      - with_pip: bool = False
      - prompt: Optional[str] = None
      - upgrade_deps: bool = False             (Since 3.9)
    """

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        logger.debug("QemuEnvBuilder.__init__(...)")
        self.script_packages = kwargs.pop("script_packages", ())
        super().__init__(*args, **kwargs)

        # The EnvBuilder class is cute and toggles this setting off
        # before post_setup, but we need it to decide if we want to
        # generate shims or not.
        self._system_site_packages = self.system_site_packages

    def post_setup(self, context: SimpleNamespace) -> None:
        logger.debug("post_setup(...)")

        # Generate console_script entry points for system packages:
        if self._system_site_packages:
            generate_console_scripts(
                context.env_exe, context.bin_path, self.script_packages
            )

        # print the python executable to stdout for configure.
        print(context.env_exe)


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


def check_ensurepip(with_pip: bool) -> None:
    """
    Check that we have ensurepip.

    Raise a fatal exception with a helpful hint if it isn't available.
    """
    if not with_pip:
        return

    if not find_spec("ensurepip"):
        msg = (
            "Python's ensurepip module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "Either install ensurepip, or alleviate the need for it in the "
            "first place by installing pip and setuptools for "
            f"'{sys.executable}'.\n"
            "(Hint: Debian puts ensurepip in its python3-venv package.)"
        )
        raise Ouch(msg)

    # ensurepip uses pyexpat, which can also go missing on us:
    if not find_spec("pyexpat"):
        msg = (
            "Python's pyexpat module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "Either install pyexpat, or alleviate the need for it in the "
            "first place by installing pip and setuptools for "
            f"'{sys.executable}'.\n\n"
            "(Hint: NetBSD's pkgsrc debundles this to e.g. 'py310-expat'.)"
        )
        raise Ouch(msg)


def make_venv(  # pylint: disable=too-many-arguments
    env_dir: Union[str, Path],
    system_site_packages: bool = False,
    clear: bool = True,
    symlinks: Optional[bool] = None,
    with_pip: Optional[bool] = None,
    script_packages: Sequence[str] = (),
) -> None:
    """
    Create a venv using `QemuEnvBuilder`.

    This is analogous to the `venv.create` module-level convenience
    function that is part of the Python stdblib, except it uses
    `QemuEnvBuilder` instead.

    :param env_dir: The directory to create/install to.
    :param system_site_packages:
        Allow inheriting packages from the system installation.
    :param clear: When True, fully remove any prior venv and files.
    :param symlinks:
        Whether to use symlinks to the target interpreter or not. If
        left unspecified, it will use symlinks except on Windows to
        match behavior with the "venv" CLI tool.
    :param with_pip:
        Whether to run "ensurepip" or not. If unspecified, this will
        default to False if system_site_packages is True and a usable
        version of pip is found.
    :param script_packages:
        A sequence of package names to generate console entry point
        shims for, when system_site_packages is True.
    """
    logging.debug(
        "%s: make_venv(env_dir=%s, system_site_packages=%s, "
        "clear=%s, symlinks=%s, with_pip=%s, script_packages=%s)",
        __file__,
        str(env_dir),
        system_site_packages,
        clear,
        symlinks,
        with_pip,
        script_packages,
    )

    print(f"MKVENV {str(env_dir)}", file=sys.stderr)

    # ensurepip is slow: venv creation can be very fast for cases where
    # we allow the use of system_site_packages. Toggle ensure_pip on only
    # in the cases where we really need it.
    if with_pip is None:
        with_pip = True if not system_site_packages else need_ensurepip()
        logger.debug("with_pip unset, choosing %s", with_pip)

    check_ensurepip(with_pip)

    if symlinks is None:
        # Default behavior of standard venv CLI
        symlinks = os.name != "nt"

    builder = QemuEnvBuilder(
        system_site_packages=system_site_packages,
        clear=clear,
        symlinks=symlinks,
        with_pip=with_pip,
        script_packages=script_packages,
    )
    try:
        logger.debug("Invoking builder.create()")
        try:
            builder.create(str(env_dir))
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

        def _stringify(data: Optional[Union[str, bytes]]) -> Optional[str]:
            if isinstance(data, bytes):
                return data.decode()
            return data

        if exc.stdout:
            print(
                "========== stdout ==========",
                _stringify(exc.stdout),
                "============================",
                sep="\n",
                file=sys.stderr,
            )
        if exc.stderr:
            print(
                "========== stderr ==========",
                _stringify(exc.stderr),
                "============================",
                sep="\n",
                file=sys.stderr,
            )
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
        r"(?P<module>[\w.]+)\s*"
        r"(:\s*(?P<attr>[\w.]+)\s*)?"
        r"((?P<extras>\[.*\])\s*)?$"
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
                lambda ep: ep.group == "console_scripts", entry_points
            )

            for entry_point in entry_points:
                # Python 3.8 doesn't have 'module' or 'attr' attributes
                if not (
                    hasattr(entry_point, "module")
                    and hasattr(entry_point, "attr")
                ):
                    match = pattern.match(entry_point.value)
                    assert match is not None
                    module = match.group("module")
                    attr = match.group("attr")
                else:
                    module = entry_point.module
                    attr = entry_point.attr
                yield {
                    "name": entry_point.name,
                    "module": module,
                    "import_name": attr,
                    "func": attr,
                }

    return _generator()


def _gen_pkg_resources(packages: Sequence[str]) -> Iterator[Dict[str, str]]:
    # pylint: disable=import-outside-toplevel
    # Bundled with setuptools; has a good chance of being available.
    import pkg_resources

    def _generator() -> Iterator[Dict[str, str]]:
        for package in packages:
            try:
                eps = pkg_resources.get_entry_map(package, "console_scripts")
            except pkg_resources.DistributionNotFound:
                continue

            for entry_point in eps.values():
                yield {
                    "name": entry_point.name,
                    "module": entry_point.module_name,
                    "import_name": ".".join(entry_point.attrs),
                    "func": ".".join(entry_point.attrs),
                }

    return _generator()


# Borrowed/adapted from pip's vendored version of distutils:
SCRIPT_TEMPLATE = r"""#!{python_path:s}
# -*- coding: utf-8 -*-
import re
import sys
from {module:s} import {import_name:s}
if __name__ == '__main__':
    sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])
    sys.exit({func:s}())
"""


def generate_console_scripts(
    python_path: str, bin_path: str, packages: Sequence[str]
) -> None:
    """
    Generate script shims for console_script entry points in @packages.
    """
    if not packages:
        return

    def _get_entry_points() -> Iterator[Dict[str, str]]:
        """Python 3.7 compatibility shim for iterating entry points."""
        # Python 3.8+, or Python 3.7 with importlib_metadata installed.
        try:
            return _gen_importlib(packages)
        except ImportError as exc:
            logger.debug("%s", str(exc))

        # Python 3.7 with setuptools installed.
        try:
            return _gen_pkg_resources(packages)
        except ImportError as exc:
            logger.debug("%s", str(exc))
            raise Ouch(
                "Neither importlib.metadata nor pkg_resources found, "
                "can't generate console script shims.\n"
                "Use Python 3.8+, or install importlib-metadata or setuptools."
            ) from exc

    for entry_point in _get_entry_points():
        script_path = os.path.join(bin_path, entry_point["name"])
        script = SCRIPT_TEMPLATE.format(python_path=python_path, **entry_point)
        with open(script_path, "w", encoding="UTF-8") as file:
            file.write(script)
        mode = os.stat(script_path).st_mode | stat.S_IEXEC
        os.chmod(script_path, mode)

        logger.debug("wrote '%s'", script_path)


def _add_create_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser("create", help="create a venv")
    subparser.add_argument(
        "--gen",
        type=str,
        action="append",
        help="Regenerate console_scripts for given packages, if found.",
    )
    subparser.add_argument(
        "target",
        type=str,
        action="store",
        help="Target directory to install virtual environment into.",
    )


def main() -> int:
    """CLI interface to make_qemu_venv. See module docstring."""
    if os.environ.get("DEBUG") or os.environ.get("GITLAB_CI"):
        # You're welcome.
        logging.basicConfig(level=logging.DEBUG)
    elif os.environ.get("V"):
        logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(
        prog="mkvenv",
        description="QEMU pyvenv bootstrapping utility",
    )
    subparsers = parser.add_subparsers(
        title="Commands",
        dest="command",
        required=True,
        metavar="command",
        help="Description",
    )

    _add_create_subcommand(subparsers)

    args = parser.parse_args()
    try:
        if args.command == "create":
            script_packages = []
            for element in args.gen or ():
                script_packages.extend(element.split(","))
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
                script_packages=script_packages,
            )
        logger.debug("mkvenv.py %s: exiting", args.command)
    except Ouch as exc:
        print("\n*** Ouch! ***\n", file=sys.stderr)
        print(str(exc), "\n\n", file=sys.stderr)
        return 1
    except:  # pylint: disable=bare-except
        print("mkvenv did not complete successfully:", file=sys.stderr)
        traceback.print_exc()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
