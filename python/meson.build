
tox = find_program('tox', required: false)
toxcfg = ['-c', meson.current_source_dir(),
          '--workdir', meson.current_source_dir(),
          '--root', meson.current_source_dir()]
pythonlints = ['pylint', 'mypy', 'flake8', 'isort']

pythontests = [
    {
        'dir': meson.current_source_dir() / 'qemu',
        'pylint': true,
        'mypy': true,
        'flake8': true,
        'isort': true,
    },
    {
        'dir': meson.current_source_dir() / 'scripts',
        'pylint': true,
        'mypy': true,
        'flake8': true,
        'isort': true,
    },
]

pyenvs = []
if tox.found() and get_option('wrap_mode') != 'nodownload'
    rc = run_command([tox, 'list'] + toxcfg,
                     capture: true,
                     check: true)

    foreach envline: rc.stdout().strip().split('\n')
        if envline.startswith('py')
            pyenv = envline.split(' ')[0]
            message('Adding python environment: ' + pyenv)

            mkenv = custom_target('tox-env-' + pyenv,
                                  command: [tox, 'run', '-e', pyenv, '--notest'] + toxcfg,
                                  output: pyenv,
                                  build_by_default: false)
            pyenvs += { 'name': pyenv, 'version': pyenv.substring(2), 'target': mkenv }
        endif
    endforeach
endif
