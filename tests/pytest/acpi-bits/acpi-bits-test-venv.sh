#!/usr/bin/env bash
# Generates a python virtual environment for the test to run.
# Then runs python test scripts from within that virtual environment.
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
# Author: Ani Sinha <ani@anisinha.ca>

set -e

MYPATH=$(realpath ${BASH_SOURCE:-$0})
MYDIR=$(dirname $MYPATH)

if [ -z "$PYTEST_SOURCE_ROOT" ]; then
    echo -n "Please set QTEST_SOURCE_ROOT env pointing"
    echo " to the root of the qemu source tree."
    echo -n "This is required so that the test can find the "
    echo "python modules that it needs for execution."
    exit 1
fi
SRCDIR=$PYTEST_SOURCE_ROOT
TESTSCRIPTS=("acpi-bits-test.py")
PIPCMD="-m pip -q --disable-pip-version-check"
# we need to save the old value of PWD before we do a change-dir later
PYTEST_PWD=$PWD

TESTS_PYTHON=/usr/bin/python3
TESTS_VENV_REQ=requirements.txt

# sadly for pip -e and -t options do not work together.
# please see https://github.com/pypa/pip/issues/562
cd $MYDIR

$TESTS_PYTHON -m venv .
$TESTS_PYTHON $PIPCMD install -e $SRCDIR/python/
[ -f $TESTS_VENV_REQ ] && \
    $TESTS_PYTHON $PIPCMD install -r $TESTS_VENV_REQ || exit 0

# venv is activated at this point.

# run the test
for testscript in ${TESTSCRIPTS[@]} ; do
    export PYTEST_PWD; python3 $testscript
done

cd $PYTEST_PWD

exit 0
