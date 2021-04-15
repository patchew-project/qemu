#!/usr/bin/env python3

import sys
import json

from avocado.core.job import Job

from utils import meson_introspect_to_avocado_suite


def main():
    config = {'run.test_runner': 'nrunner'}
    if len(sys.argv) > 1:
        config['run.results_dir'] = sys.argv[1]

    suite = meson_introspect_to_avocado_suite(json.load(sys.stdin),
                                              'qtest-unit',
                                              config)
    with Job(config, [suite]) as j:
        return j.run()


if __name__ == '__main__':
    sys.exit(main())
