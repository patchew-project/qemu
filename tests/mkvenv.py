"""
mkvenv - QEMU venv bootstrapping utility

usage: mkvenv.py [-h] [--offline] [--opt OPT] target

Bootstrap QEMU testing venv.

positional arguments:
  target      Target directory to install virtual environment into.

optional arguments:
  -h, --help  show this help message and exit
  --offline   Use system packages and work entirely offline.
  --opt OPT   Install an optional dependency group.
"""

# Copyright (C) 2022 Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

# Do not add any dependencies from outside the stdlib:
# This script must be usable on its own!

import argparse
from contextlib import contextmanager
import logging
import os
from pathlib import Path
import subprocess
import sys
from typing import Iterable, Iterator, Union
import venv


def make_venv(
        venv_path: Union[str, Path],
        system_site_packages: bool = False
) -> None:
    """
    Create a venv using the python3 'venv' module.

    Identical to:
    ``python3 -m venv --clear [--system-site-packages] {venv_path}``

    :param venv_path: The target directory
    :param system_site_packages: If True, allow system packages in venv.
    """
    logging.debug("%s: make_venv(venv_path=%s, system_site_packages=%s)",
                  __file__, str(venv_path), system_site_packages)
    venv.create(
        str(venv_path),
        clear=True,
        symlinks=os.name != "nt",  # Same as venv CLI's default.
        with_pip=True,
        system_site_packages=system_site_packages,
    )


@contextmanager
def enter_venv(venv_dir: Union[str, Path]) -> Iterator[None]:
    """Scoped activation of an existing venv."""
    venv_keys = ('PATH', 'PYTHONHOME', 'VIRTUAL_ENV')
    old = {k: v for k, v in os.environ.items() if k in venv_keys}
    vdir = Path(venv_dir).resolve()

    def _delete_keys() -> None:
        for k in venv_keys:
            try:
                del os.environ[k]
            except KeyError:
                pass

    try:
        _delete_keys()

        os.environ['VIRTUAL_ENV'] = str(vdir)
        os.environ['PATH'] = os.pathsep.join([
            str(vdir / "bin/"),
            old['PATH'],
        ])

        logging.debug("PATH => %s", os.environ['PATH'])
        logging.debug("VIRTUAL_ENV => %s", os.environ['VIRTUAL_ENV'])

        yield

    finally:
        _delete_keys()
        for key, val in old.items():
            os.environ[key] = val


def install(*args: str, offline: bool = False) -> None:
    """Shorthand for pip install; expected to be used under a venv."""
    cli_args = ['pip3', '--disable-pip-version-check', '-q', 'install']
    if offline:
        cli_args.append('--no-index')
    cli_args += args
    print(f"  VENVPIP install {' '.join(args)}")
    logging.debug(' '.join(cli_args))
    subprocess.run(cli_args, check=True)


def make_qemu_venv(
        venv_dir: str,
        options: Iterable[str],
        offline: bool = False
) -> None:
    """
    Make and initialize a qemu testing venv.

    Forcibly re-creates the venv if it already exists, unless optional
    dependency groups are specified - in which case, just install the
    extra dependency groups.

    :param venv_dir: The target directory to install to.
    :param options:
        Optional dependency groups of the testing package to install.
        (e.g. 'avocado'.)
    :param offline:
        If true, force offline mode. System packages will be usable from
        the venv and dependencies will not be fetched from PyPI.
    """
    venv_path = Path(venv_dir).resolve()
    src_root = Path(__file__).joinpath('../..')
    pysrc_path = src_root.joinpath('python/').resolve()
    test_src_path = src_root.joinpath('tests/').resolve()

    logging.debug("make_qemu_venv(%s)", venv_dir)
    logging.debug("sys.executable: %s", sys.executable)
    logging.debug("resolved:       %s", str(Path(sys.executable).resolve()))
    logging.debug("venv_dir:       %s", str(venv_path))
    logging.debug("python source:  %s", str(pysrc_path))
    logging.debug("tests source:   %s", str(test_src_path))

    do_initialize = not venv_path.exists() or not options
    if do_initialize:
        make_venv(venv_path, system_site_packages=offline)

    with enter_venv(venv_path):
        if do_initialize:
            install("-e", str(pysrc_path), offline=offline)
            install("--no-binary", "qemu.dummy-tests",
                    str(test_src_path), offline=offline)
            venv_path.touch()

        for option in options:
            dummy = venv_path / option
            install(f"{test_src_path!s}/[{option}]", offline=offline)
            dummy.touch()


def main() -> int:
    """CLI interface to make_qemu_venv. See module docstring."""
    if os.environ.get('DEBUG'):
        logging.basicConfig(level=logging.DEBUG)

    parser = argparse.ArgumentParser(
        description="Bootstrap QEMU testing venv.")
    parser.add_argument(
        '--offline',
        action='store_true',
        help="Use system packages and work entirely offline.",
    )
    parser.add_argument(
        '--opt',
        type=str,
        action='append',
        help="Install an optional dependency group.",
    )
    parser.add_argument(
        'target',
        type=str,
        action='store',
        help="Target directory to install virtual environment into.",
    )
    args = parser.parse_args()
    make_qemu_venv(args.target, args.opt or (), args.offline)
    return 0


if __name__ == '__main__':
    sys.exit(main())
