"""
mkvenv - QEMU pyvenv bootstrapping utility

usage: mkvenv [-h] command ...

QEMU pyvenv bootstrapping utility

options:
  -h, --help  show this help message and exit

Commands:
  command     Description
    create    create a venv
    post_init
              post-venv initialization
    ensure    Ensure that the specified package is installed.

--------------------------------------------------

usage: mkvenv create [-h] [--gen GEN] target

positional arguments:
  target      Target directory to install virtual environment into.

options:
  -h, --help  show this help message and exit
  --gen GEN   Regenerate console_scripts for given packages, if found.

--------------------------------------------------

usage: mkvenv post_init [-h] [--gen GEN] [--binpath BINPATH]

options:
  -h, --help         show this help message and exit
  --gen GEN          Regenerate console_scripts for given packages, if found.
  --binpath BINPATH  Path where console script shims should be generated

--------------------------------------------------

usage: mkvenv ensure [-h] [--online] [--dir DIR] dep_spec

positional arguments:
  dep_spec    PEP 508 Dependency specification, e.g. 'meson>=0.61.5'

options:
  -h, --help  show this help message and exit
  --online    Install packages from PyPI, if necessary.
  --dir DIR   Path to vendored packages where we may install from.

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
import site
import stat
import subprocess
import sys
import sysconfig
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

DirType = Union[str, bytes, "os.PathLike[str]", "os.PathLike[bytes]"]
logger = logging.getLogger("mkvenv")


def inside_a_venv() -> bool:
    """Returns True if it is executed inside of a virtual environment."""
    return sys.prefix != sys.base_prefix


class Ouch(RuntimeError):
    """An Exception class we can't confuse with a builtin."""


class QemuEnvBuilder(venv.EnvBuilder):
    """
    An extension of venv.EnvBuilder for building QEMU's configure-time venv.

    The primary differences are:

    (1) It adds the ability to regenerate console_script shims for
    packages available via system_site_packages for any packages
    specified by the 'script_packages' argument

    (2) It emulates a "nested" virtual environment when invoked from
    inside of an existing virtual environment by including packages from
    the parent.

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

        # For nested venv emulation:
        self.use_parent_packages = False
        if inside_a_venv():
            # Include parent packages only if we're in a venv and
            # system_site_packages was True.
            self.use_parent_packages = kwargs.pop(
                "system_site_packages", False
            )
            # Include system_site_packages only when the parent,
            # The venv we are currently in, also does so.
            kwargs["system_site_packages"] = sys.base_prefix in site.PREFIXES

        super().__init__(*args, **kwargs)

        # Make the context available post-creation:
        self._context: Optional[SimpleNamespace] = None

    def compute_venv_libpath(self, context: SimpleNamespace) -> str:
        """
        Compatibility wrapper for context.lib_path for Python < 3.12
        """
        # Python 3.12+, not strictly necessary because it's documented
        # to be the same as 3.10 code below:
        if sys.version_info >= (3, 12):
            return context.lib_path

        # Python 3.10+
        if "venv" in sysconfig.get_scheme_names():
            return sysconfig.get_path(
                "purelib", scheme="venv", vars={"base": context.env_dir}
            )

        # For Python <= 3.9 we need to hardcode this. Fortunately the
        # code below was the same in Python 3.6-3.10, so there is only
        # one case.
        if sys.platform == "win32":
            return os.path.join(context.env_dir, "Lib", "site-packages")
        return os.path.join(
            context.env_dir,
            "lib",
            "python%d.%d" % sys.version_info[:2],
            "site-packages",
        )

    def ensure_directories(self, env_dir: DirType) -> SimpleNamespace:
        logger.debug("ensure_directories(env_dir=%s)", env_dir)
        self._context = super().ensure_directories(env_dir)
        return self._context

    def create(self, env_dir: DirType) -> None:
        logger.debug("create(env_dir=%s)", env_dir)
        super().create(env_dir)
        assert self._context is not None
        self.post_post_setup(self._context)

    def post_setup(self, context: SimpleNamespace) -> None:
        logger.debug("post_setup(...)")
        # print the python executable to stdout for configure.
        print(context.env_exe)

    def post_post_setup(self, context: SimpleNamespace) -> None:
        """
        The final, final hook. Enter the venv and run commands inside of it.
        """
        if self.use_parent_packages:
            # We're inside of a venv and we want to include the parent
            # venv's packages.
            parent_libpath = sysconfig.get_path("purelib")
            logger.debug("parent_libpath: %s", parent_libpath)

            our_libpath = self.compute_venv_libpath(context)
            logger.debug("our_libpath: %s", our_libpath)

            pth_file = os.path.join(our_libpath, "nested.pth")
            with open(pth_file, "w", encoding="UTF-8") as file:
                file.write(parent_libpath + os.linesep)

        args = [
            context.env_exe,
            __file__,
            "post_init",
            "--binpath",
            context.bin_path,
        ]
        if self.system_site_packages:
            scripts = ",".join(self.script_packages)
            if scripts:
                args += ["--gen", scripts]
        subprocess.run(args, check=True)


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


def check_ensurepip(prefix: str = "", suggest_remedy: bool = False) -> None:
    """
    Check that we have ensurepip.

    Raise a fatal exception with a helpful hint if it isn't available.
    """
    if not find_spec("ensurepip"):
        msg = (
            "Python's ensurepip module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "(Debian puts ensurepip in its python3-venv package.)\n"
        )
        if suggest_remedy:
            msg += (
                "Either install ensurepip, or alleviate the need for it in the"
                " first place by installing pip and setuptools for "
                f"'{sys.executable}'.\n"
            )
        raise Ouch(prefix + msg)

    # ensurepip uses pyexpat, which can also go missing on us:
    if not find_spec("pyexpat"):
        msg = (
            "Python's pyexpat module is not found.\n"
            "It's normally part of the Python standard library, "
            "maybe your distribution packages it separately?\n"
            "(NetBSD's pkgsrc debundles this to e.g. 'py310-expat'.)\n"
        )
        if suggest_remedy:
            msg += (
                "Either install pyexpat, or alleviate the need for it in the "
                "first place by installing pip and setuptools for "
                f"'{sys.executable}'.\n"
            )
        raise Ouch(prefix + msg)


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

    if with_pip:
        check_ensurepip(suggest_remedy=True)

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
    logger.debug(
        "generate_console_scripts(python_path=%s, bin_path=%s, packages=%s)",
        python_path,
        bin_path,
        packages,
    )

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


def checkpip() -> None:
    """
    Debian10 has a pip that's broken when used inside of a virtual environment.

    We try to detect and correct that case here.
    """
    try:
        # pylint: disable=import-outside-toplevel, unused-import
        import pip._internal  # noqa: F401

        logger.debug("pip appears to be working correctly.")
        return
    except ModuleNotFoundError as exc:
        if exc.name == "pip._internal":
            # Uh, fair enough. They did say "internal".
            # Let's just assume it's fine.
            return
        logger.warning("pip appears to be malfunctioning: %s", str(exc))

    check_ensurepip("pip appears to be non-functional, and ")

    logging.debug("Attempting to repair pip ...")
    subprocess.run(
        (sys.executable, "-m", "ensurepip"),
        stdout=subprocess.DEVNULL,
        check=True,
    )
    logging.debug("Pip is now (hopefully) repaired!")


def pip_install(
    args: Sequence[str],
    online: bool = False,
    wheels_dir: Optional[Union[str, Path]] = None,
) -> None:
    """
    Use pip to install a package or package(s) as specified in @args.
    """
    loud = bool(
        os.environ.get("DEBUG")
        or os.environ.get("GITLAB_CI")
        or os.environ.get("V")
    )

    full_args = [
        sys.executable,
        "-m",
        "pip",
        "install",
        "--disable-pip-version-check",
        "-v" if loud else "-q",
    ]
    if not online:
        full_args += ["--no-index"]
    if wheels_dir:
        full_args += ["--find-links", f"file://{str(wheels_dir)}"]
    full_args += list(args)
    subprocess.run(full_args, check=True)


def ensure(
    dep_spec: str,
    online: bool = False,
    wheels_dir: Optional[Union[str, Path]] = None,
) -> None:
    """
    Use pip to ensure we have the package specified by @dep_spec.

    If the package is already installed, do nothing. If online and
    wheels_dir are both provided, prefer packages found in wheels_dir
    first before connecting to PyPI.

    :param dep_spec:
        A PEP 508 dependency specification. e.g. 'meson>=0.61.5'.
    :param online: If True, fall back to PyPI.
    :param wheels_dir: If specified, search this path for packages.
    """
    # This first install command will:
    # (A) Do nothing, if we already have a suitable package.
    # (B) Install the package from vendored source, if possible.
    # (C) Fail if neither A nor B.
    try:
        pip_install([dep_spec], online=False, wheels_dir=wheels_dir)
        # (A) or (B) happened. Success.
        return
    except subprocess.CalledProcessError:
        # (C) Happened.
        # The package is missing or isn't a suitable version,
        # and we weren't able to install a suitable vendored package.
        if online:
            pip_install([dep_spec], online=True)
        else:
            raise


def post_venv_setup(bin_path: str, packages: Sequence[str] = ()) -> None:
    """
    This is intended to be run *inside the venv* after it is created.
    """
    python_path = sys.executable
    logger.debug(
        "post_venv_setup(bin_path=%s, packages=%s)", bin_path, packages
    )
    generate_console_scripts(python_path, bin_path, packages)

    # Test for a broken pip (Debian 10 or derivative?) and fix it if needed
    checkpip()


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


def _add_post_init_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser(
        "post_init", help="post-venv initialization"
    )
    subparser.add_argument(
        "--gen",
        type=str,
        action="append",
        help="Regenerate console_scripts for given packages, if found.",
    )
    subparser.add_argument(
        "--binpath",
        type=str,
        action="store",
        help="Path where console script shims should be generated",
    )


def _add_ensure_subcommand(subparsers: Any) -> None:
    subparser = subparsers.add_parser(
        "ensure", help="Ensure that the specified package is installed."
    )
    subparser.add_argument(
        "--online",
        action="store_true",
        help="Install packages from PyPI, if necessary.",
    )
    subparser.add_argument(
        "--dir",
        type=str,
        action="store",
        help="Path to vendored packages where we may install from.",
    )
    subparser.add_argument(
        "dep_spec",
        type=str,
        action="store",
        help="PEP 508 Dependency specification, e.g. 'meson>=0.61.5'",
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
    _add_post_init_subcommand(subparsers)
    _add_ensure_subcommand(subparsers)

    args = parser.parse_args()
    script_packages = []

    def _normalize_gen() -> None:
        for element in args.gen or ():
            script_packages.extend(element.split(","))

    try:
        if args.command == "create":
            _normalize_gen()
            make_venv(
                args.target,
                system_site_packages=True,
                clear=True,
                script_packages=script_packages,
            )
        if args.command == "post_init":
            _normalize_gen()
            post_venv_setup(args.binpath, script_packages)
        if args.command == "ensure":
            ensure(
                dep_spec=args.dep_spec,
                online=args.online,
                wheels_dir=args.dir,
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
