#!/usr/bin/env python3

import json
import random
import sys

from avocado.core.job import Job
from avocado.core.resolver import resolve
from avocado.core.suite import resolutions_to_runnables

from utils import meson_introspect_to_avocado_suite


def main():
    config = {'run.test_runner': 'nrunner'}
    if len(sys.argv) > 1:
        config['run.results_dir'] = sys.argv[1]

    suite = meson_introspect_to_avocado_suite(json.load(sys.stdin),
                                              'qtest-unit-acceptance',
                                              config)
    acceptance = resolutions_to_runnables(resolve(["tests/acceptance"]),
                                          config)
    suite.tests += acceptance
    random.shuffle(suite.tests)
    with Job(config, [suite]) as j:
        return j.run()


if __name__ == '__main__':
    sys.exit(main())
