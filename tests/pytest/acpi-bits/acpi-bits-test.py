#!/usr/bin/env python3
# group: rw quick
# Exercize QEMU generated ACPI/SMBIOS tables using biosbits,
# https://biosbits.org/
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Some parts are slightly taken from qtest.py and iotests.py
#
# Authors:
#  Ani Sinha <ani@anisinha.ca>

# pylint: disable=invalid-name

"""
QEMU bios tests using biosbits available at
https://biosbits.org/.
"""

import logging
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from urllib import request
import zipfile
from typing import (
    List,
    Optional,
    Sequence,
)
from tap import TAPTestRunner
from qemu.machine import QEMUMachine

PYTESTQEMUBIN = os.getenv('PYTEST_QEMU_BINARY')
PYTEST_PWD = os.getenv('PYTEST_PWD')

def get_arch():
    """finds the arch from the qemu binary name"""
    match = re.search('.*qemu-system-(.*)', PYTESTQEMUBIN)
    if match:
        return match.group(1)
    return 'x86_64'

ARCH = get_arch()

class QEMUBitsMachine(QEMUMachine):
    """
    A QEMU VM, with isa-debugcon enabled and bits iso passed
    using -cdrom to QEMU commandline.
    """
    def __init__(self,
                 binary: str,
                 args: Sequence[str] = (),
                 wrapper: Sequence[str] = (),
                 name: Optional[str] = None,
                 base_temp_dir: str = "/var/tmp",
                 debugcon_log: str = "debugcon-log.txt",
                 debugcon_addr: str = "0x403",
                 sock_dir: Optional[str] = None,
                 qmp_timer: Optional[float] = None):
        # pylint: disable=too-many-arguments

        if name is None:
            name = "qemu-bits-%d" % os.getpid()
        if sock_dir is None:
            sock_dir = base_temp_dir
        super().__init__(binary, args, wrapper=wrapper, name=name,
                         base_temp_dir=base_temp_dir,
                         sock_dir=sock_dir, qmp_timer=qmp_timer)
        self.debugcon_log = debugcon_log
        self.debugcon_addr = debugcon_addr
        self.base_temp_dir = base_temp_dir

    @property
    def _base_args(self) -> List[str]:
        args = super()._base_args
        args.extend([
            '-chardev',
            'file,path=%s,id=debugcon' %os.path.join(self.base_temp_dir,
                                                     self.debugcon_log),
            '-device',
            'isa-debugcon,iobase=%s,chardev=debugcon' %self.debugcon_addr,
        ])
        return args

    def base_args(self):
        """return the base argument to QEMU binary"""
        return self._base_args

class AcpiBitsTest(unittest.TestCase):
    """ACPI and SMBIOS tests using biosbits."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._vm = None
        self._workDir = None
        self._bitsVer = 2100
        self._bitsLoc = "https://github.com/ani-sinha/bits/raw/bits-builds/"
        self._debugcon_addr = '0x403'
        self._debugcon_log = 'debugcon-log.txt'
        logging.basicConfig(level=logging.INFO)

    def copy_bits_config(self):
        """ copies the bios bits config file into bits.
        """
        config_file = 'bits-cfg.txt'
        qemu_bits_config_dir = os.path.join(os.getcwd(), 'bits-config')
        target_config_dir = os.path.join(self._workDir,
                                         'bits-%d' %self._bitsVer, 'boot')
        self.assertTrue(os.path.exists(qemu_bits_config_dir))
        self.assertTrue(os.path.exists(target_config_dir))
        self.assertTrue(os.access(os.path.join(qemu_bits_config_dir,
                                               config_file), os.R_OK))
        shutil.copy2(os.path.join(qemu_bits_config_dir, config_file),
                     target_config_dir)
        logging.info('copied config file %s to %s',
                     config_file, target_config_dir)

    def copy_test_scripts(self):
        """copies the python test scripts into bits. """
        qemu_test_dir = os.path.join(os.getcwd(), 'bits-tests')
        target_test_dir = os.path.join(self._workDir, 'bits-%d' %self._bitsVer,
                                       'boot', 'python')

        self.assertTrue(os.path.exists(qemu_test_dir))
        self.assertTrue(os.path.exists(target_test_dir))

        for filename in os.listdir(qemu_test_dir):
            if os.path.isfile(os.path.join(qemu_test_dir, filename)) and \
               filename.endswith('.py'):
                shutil.copy2(os.path.join(qemu_test_dir, filename),
                             target_test_dir)
                logging.info('copied test file %s to %s',
                             filename, target_test_dir)

                # now remove the pyc test file if it exists, otherwise the
                # changes in the python test script won't be executed.
                testfile_pyc = os.path.splitext(filename)[0] + '.pyc'
                if os.access(os.path.join(target_test_dir, testfile_pyc),
                             os.F_OK):
                    os.remove(os.path.join(target_test_dir, testfile_pyc))
                    logging.info('removed compiled file %s',
                                 os.path.join(target_test_dir, testfile_pyc))

    def fix_mkrescue(self, mkrescue):
        """ grub-mkrescue is a bash script with two variables, 'prefix' and
            'libdir'. They must be pointed to the right location so that the
            iso can be generated appropriately. We point the two variables to
            the directory where we have extracted our pre-built bits grub
            tarball.
        """
        grub_x86_64_mods = os.path.join(self._workDir, 'grub-inst-x86_64-efi')
        grub_i386_mods = os.path.join(self._workDir, 'grub-inst')

        self.assertTrue(os.path.exists(grub_x86_64_mods))
        self.assertTrue(os.path.exists(grub_i386_mods))

        new_script = ""
        with open(mkrescue, 'r') as filehandle:
            orig_script = filehandle.read()
            new_script = re.sub('(^prefix=)(.*)',
                                r'\1"%s"' %grub_x86_64_mods,
                                orig_script, flags=re.M)
            new_script = re.sub('(^libdir=)(.*)', r'\1"%s/lib"' %grub_i386_mods,
                                new_script, flags=re.M)

        with open(mkrescue, 'w') as filehandle:
            filehandle.write(new_script)

    def generate_bits_iso(self):
        """ Uses grub-mkrescue to generate a fresh bits iso with the python
            test scripts
        """
        bits_dir = os.path.join(self._workDir, 'bits-%d' %self._bitsVer)
        iso_file = os.path.join(self._workDir, 'bits-%d.iso' %self._bitsVer)
        mkrescue_script = os.path.join(self._workDir,
                                       'grub-inst-x86_64-efi', 'bin',
                                       'grub-mkrescue')

        self.assertTrue(os.access(mkrescue_script,
                                  os.R_OK | os.W_OK | os.X_OK))

        self.fix_mkrescue(mkrescue_script)

        logging.info('calling grub-mkrescue to generate the biosbits iso ...')

        try:
            if os.getenv('V'):
                subprocess.check_call([mkrescue_script, '-o',
                                       iso_file, bits_dir],
                                      stdout=subprocess.DEVNULL)
            else:
                subprocess.check_call([mkrescue_script, '-o',
                                       iso_file, bits_dir],
                                      stderr=subprocess.DEVNULL,
                                      stdout=subprocess.DEVNULL)
        except Exception as e: # pylint: disable=broad-except
            self.skipTest("Error while generating the bits iso. "
                          "Pass V=1 in the environment to get more details. "
                          + str(e))

        self.assertTrue(os.access(iso_file, os.R_OK))

        logging.info('iso file %s successfully generated.', iso_file)

    def setUp(self):
        BITS_LOC = os.getenv("PYTEST_BITSLOC")
        if BITS_LOC:
            prefix = BITS_LOC
        else:
            prefix = os.path.join(os.getcwd(), 'prebuilt')
            if not os.path.isdir(prefix):
                os.mkdir(prefix, mode=0o775)

        bits_zip_file = os.path.join(prefix, 'bits-%d.zip'
                                     %self._bitsVer)
        grub_tar_file = os.path.join(prefix,
                                     'bits-%d-grub.tar.gz' %self._bitsVer)
        # if the location of the bits binaries has been specified by the user
        # and they are not found in that location, skip the test.
        if BITS_LOC and not os.access(bits_zip_file, os.F_OK):
            self.skipTest("test skipped since biosbits binaries " +
                          "could not be found in the specified location %s." \
                          %BITS_LOC)
        if BITS_LOC and not os.access(grub_tar_file, os.F_OK):
            self.skipTest("test skipped since biosbits binaries " +
                          "could not be found in the specified location %s." \
                          %BITS_LOC)

        self._workDir = tempfile.mkdtemp(prefix='acpi-bits-',
                                         suffix='.tmp')
        logging.info('working dir: %s', self._workDir)

        localArchive = "bits-%d.zip" % self._bitsVer
        if not os.access(bits_zip_file, os.F_OK):
            logging.info("archive %s not found in %s, downloading ...",
                         localArchive, bits_zip_file)
            try:
                req = request.urlopen(self._bitsLoc + localArchive)
                with open(os.path.join(prefix, localArchive),
                          'wb') as archivef:
                    archivef.write(req.read())
            except Exception as e: # pylint: disable=broad-except
                self.skipTest("test skipped since biosbits binaries " +
                              "could not be obtained." + str(e))
        else:
            logging.info('using locally found %s', localArchive)

        localArchive = "bits-%d-grub.tar.gz" % self._bitsVer
        if not os.access(grub_tar_file, os.F_OK):
            logging.info("archive %s not found in %s, downloading ...",
                         localArchive, bits_zip_file)
            try:
                req = request.urlopen(self._bitsLoc + localArchive)
                with open(os.path.join(prefix, localArchive),
                          'wb') as archivef:
                    archivef.write(req.read())
            except Exception as e: # pylint: disable=broad-except
                self.skipTest("test skipped since biosbits binaries " +
                              "could not be obtained." + str(e))
        else:
            logging.info('using locally found %s', localArchive)

        # extract the bits software in the temp working directory
        with zipfile.ZipFile(bits_zip_file, 'r') as zref:
            zref.extractall(self._workDir)

        with tarfile.open(grub_tar_file, 'r') as tarball:
            tarball.extractall(self._workDir)

        self.copy_test_scripts()
        self.copy_bits_config()
        self.generate_bits_iso()

    def parse_log(self):
        """parse the log generated by running bits tests and
           check for failures.
        """
        debugconf = os.path.join(self._workDir, self._debugcon_log)
        log = ""
        with open(debugconf, 'r') as filehandle:
            log = filehandle.read()

        if os.getenv('V'):
            print('\nlogs from biosbits follows:')
            print('==========================================\n')
            print(log)
            print('==========================================\n')

        matchiter = re.finditer(r'(.*Summary: )(\d+ passed), (\d+ failed).*',
                                log)
        for match in matchiter:
            # verify that no test cases failed.
            self.assertEqual(match.group(3).split()[0], '0',
                             'Some bits tests seems to have failed. ' \
                             'Set V=1 in the environment to get the entire ' \
                             'log from bits.')

    def tearDown(self):
        if self._vm:
            self.assertFalse(not self._vm.is_running)
        logging.info('removing the work directory %s', self._workDir)
        shutil.rmtree(self._workDir)

    def test_acpi_smbios_bits(self):
        """The main test case implementaion."""

        qemu_bin = PYTESTQEMUBIN
        iso_file = os.path.join(self._workDir, 'bits-%d.iso' %self._bitsVer)

        # PYTESTQEMUBIN could be relative to the current directory
        if not os.access(PYTESTQEMUBIN, os.X_OK) and PYTEST_PWD:
            qemu_bin = os.path.join(PYTEST_PWD, PYTESTQEMUBIN)

        logging.info('QEMU binary used: %s', qemu_bin)

        self.assertTrue(os.access(qemu_bin, os.X_OK))
        self.assertTrue(os.access(iso_file, os.R_OK))

        self._vm = QEMUBitsMachine(binary=qemu_bin,
                                   base_temp_dir=self._workDir,
                                   debugcon_log=self._debugcon_log,
                                   debugcon_addr=self._debugcon_addr)

        self._vm.add_args('-cdrom', '%s' %iso_file)

        args = " ".join(str(arg) for arg in self._vm.base_args()) + \
            " " + " ".join(str(arg) for arg in self._vm.args)

        logging.info("launching QEMU vm with the following arguments: %s",
                     args)

        self._vm.launch()
        # biosbits has been configured to run all the specified test suites
        # in batch mode and then automatically initiate a vm shutdown.
        # sleep for maximum of one minute
        max_sleep_time = time.monotonic() + 60
        while self._vm.is_running() and time.monotonic() < max_sleep_time:
            time.sleep(1)

        self.assertFalse(time.monotonic() > max_sleep_time,
                         'The VM seems to have failed to shutdown in time')

        self.parse_log()

def execute_unittest(argv: List[str], debug: bool = False,
                     runner: TAPTestRunner = None) -> None:
    """Executes unittests within the calling module."""

    unittest.main(argv=argv,
                  testRunner=runner,
                  verbosity=2 if debug else 1,
                  warnings=None if sys.warnoptions else 'ignore')

def main():
    """ The main function where execution begins. """

    assert PYTESTQEMUBIN is not None, \
        "Environment variable PYTEST_QEMU_BINARY required."

    runner = TAPTestRunner()
    runner.set_stream(True)
    runner.set_format("%s/acpi-bits-test" %ARCH)
    execute_unittest(sys.argv, False, runner)

main()
