#!/usr/bin/env python3

import os
import glob
from collections import defaultdict


class TestFinder:
    def __init__(self):
        self.groups = defaultdict(set)
        self.all_tests = glob.glob('[0-9][0-9][0-9]')

        self.all_tests += [f for f in glob.iglob('test-*')
                           if not f.endswith('.out')]

        for t in self.all_tests:
            with open(t) as f:
                for line in f:
                    if line.startswith('# group: '):
                        for g in line.split()[2:]:
                            self.groups[g].add(t)

    def add_group_file(self, fname):
        with open(fname) as f:
            for line in f:
                line = line.strip()

                if (not line) or line[0] == '#':
                    continue

                words = line.split()
                test_file = words[0]
                groups = words[1:]

                if test_file not in self.all_tests:
                    print('Warning: {}: "{}" test is not found. '
                          'Skip.'.format(fname, test_file))
                    continue

                for g in groups:
                    self.groups[g].add(test_file)

    def find_tests(self, group=None):
        if group is None:
            tests = self.all_tests
        elif group not in self.groups:
            tests = []
        elif group != 'disabled' and 'disabled' in self.groups:
            tests = self.groups[group] - self.groups['disabled']
        else:
            tests = self.groups[group]

        return sorted(tests)


if __name__ == '__main__':
    import sys

    if len(sys.argv) > 2:
        print("Usage ./find_tests.py [group]")
        sys.exit(1)

    tf = TestFinder()
    if os.path.isfile('group'):
        tf.add_group_file('group')

    if len(sys.argv) == 2:
        tests = tf.find_tests(sys.argv[1])
    else:
        tests = tf.find_tests()

    print('\n'.join(tests))
