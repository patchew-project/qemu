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

usage: mkvenv create [-h] target

positional arguments:
  target      Target directory to install virtual environment into.

options:
  -h, --help  show this help message and exit

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
import subprocess
import sys
import traceback
from types import SimpleNamespace
from typing import Any, Optional, Union
import venv


# Do not add any mandatory dependencies from outside the stdlib:
# This script *must* be usable standalone!

logger = logging.getLogger("mkvenv")


class Ouch(RuntimeError):
    """An Exception class we can't confuse with a builtin."""


class QemuEnvBuilder(venv.EnvBuilder):
    """
    An extension of venv.EnvBuilder for building QEMU's configure-time venv.

    As of this commit, it does not yet do anything particularly
    different than the standard venv-creation utility. The next several
    commits will gradually change that in small commits that highlight
    each feature individually.

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
        super().__init__(*args, **kwargs)

        # The EnvBuilder class is cute and toggles this setting off
        # before post_setup, but we need it to decide if we want to
        # generate shims or not.
        self._system_site_packages = self.system_site_packages

    def post_setup(self, context: SimpleNamespace) -> None:
        logger.debug("post_setup(...)")

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
    if with_pip and not find_spec("ensurepip"):
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


def make_venv(  # pylint: disable=too-many-arguments
    env_dir: Union[str, Path],
    system_site_packages: bool = False,
    clear: bool = True,
    symlinks: Optional[bool] = None,
    with_pip: Optional[bool] = None,
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
    """
    logging.debug(
        "%s: make_venv(env_dir=%s, system_site_packages=%s, "
        "clear=%s, symlinks=%s, with_pip=%s)",
        __file__,
        str(env_dir),
        system_site_packages,
        clear,
        symlinks,
        with_pip,
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


def _add_create_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser("create", help="create a venv")
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
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
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
