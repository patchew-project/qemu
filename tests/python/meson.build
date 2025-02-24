
foreach pyenv: pyenvs
    foreach scenario: pythontests
        suites = ['python', 'python' + pyenv['version']]

        foreach lint: pythonlints
            if scenario.get(lint, false)
                name = lint + scenario['dir'].replace(meson.project_source_root(), '').replace('/', '-')
                toxargs = toxcfg + ['exec', '-e', pyenv['name'],
                                    '--',
                                    'python3', '-m', lint,
                                    scenario['dir']]
                test(name, tox, args: toxargs, suite: suites, depends: pyenv['target'])
            endif
        endforeach
    endforeach
endforeach
