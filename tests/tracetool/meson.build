# SPDX-License-Identifier: GPL-2.0-or-later

test_env = environment()
test_env.set('PYTHONPATH', meson.project_source_root() / 'scripts')
test_env.set('PYTHONIOENCODING', 'utf-8')

backends = [
    'dtrace',
    'ftrace',
    'log',
    'simple',
    'syslog',
    'ust'
]

foreach backend: backends
    test(backend,
         python,
         args: [meson.current_source_dir() / 'tracetool-test.py',
                meson.project_source_root() / 'scripts' / 'tracetool.py',
                'trace-events',
                backend],
         workdir: meson.current_source_dir(),
         suite: ['tracetool'])
endforeach
