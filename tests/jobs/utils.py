from avocado.core.suite import TestSuite
from avocado.core.nrunner import Runnable


def protocol_tap_to_runnable(entry):
    runnable = Runnable('tap',
                        entry['cmd'][0],
                        *entry['cmd'][1:],
                        **entry['env'])
    return runnable


def meson_introspect_to_avocado_suite(introspect, suite_name, config):
    tests = []
    for entry in introspect:
        if entry['protocol'] != 'tap':
            continue
        runnable = protocol_tap_to_runnable(entry)
        tests.append(runnable)

    suite = TestSuite(name=suite_name, config=config, tests=tests)
    return suite
