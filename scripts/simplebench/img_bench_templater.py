#!/usr/bin/env python3
#
# Run img-bench template tests
#
# Copyright (c) 2021 Virtuozzo International GmbH.
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


import sys
import subprocess
import re
import json

import simplebench
from results_to_text import results_to_text
from table_templater import Templater


def bench_func(env, case):
    test = templater.gen(env['data'], case['data'])

    p = subprocess.run(test, shell=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)

    if p.returncode == 0:
        try:
            m = re.search(r'Run completed in (\d+.\d+) seconds.', p.stdout)
            return {'seconds': float(m.group(1))}
        except Exception:
            return {'error': f'failed to parse qemu-img output: {p.stdout}'}
    else:
        return {'error': f'qemu-img failed: {p.returncode}: {p.stdout}'}


if __name__ == '__main__':
    if len(sys.argv) > 1:
        print("""
Usage: no arguments. Just pass template test to stdin. Template test is
a bash script, last command should be qemu-img bench (it's output is parsed
to get a result). For templating use the following synax:

  column templating: {var1|var2|...} - test will use different values in
  different columns. You may use several {} constructions in the test, in this
  case product of all choice-sets will be used.

  row templating: [var1|var2|...] - similar thing to define rows (test-cases)

Test tempalate example:

Assume you want to compare two qemu-img binaries, called qemu-img-old and
qemu-img-new in your build directory in two test-cases with 4K writes and 64K
writes. Test may look like this:

qemu_img=/path/to/qemu/build/qemu-img-{old|new}
$qemu_img create -f qcow2 /ssd/x.qcow2 1G
$qemu_img bench -c 100 -d 8 [-s 4K|-s 64K] -w -t none -n /ssd/x.qcow2

If pass it to stdin of img_bench_templater.py, the resulting comparison table
will contain two columns (for two binaries) and two rows (for two test-cases).
""")
        sys.exit()

    templater = Templater(sys.stdin.read())

    envs = [{'id': ' / '.join(x), 'data': x} for x in templater.columns]
    cases = [{'id': ' / '.join(x), 'data': x} for x in templater.rows]

    result = simplebench.bench(bench_func, envs, cases, count=5,
                               initial_run=False)
    print(results_to_text(result))
    with open('results.json', 'w') as f:
        json.dump(result, f, indent=4)
