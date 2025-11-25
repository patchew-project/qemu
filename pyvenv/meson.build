# Note that this file only controls "optional" dependency groups; groups
# *required* for the build are handled directly in configure instead:
# namely, meson and sphinx.

pyvenv_common_deps = files(
    meson.project_source_root() + '/pythondeps.toml',
    meson.project_source_root() + '/python/scripts/mkvenv.py'
)
pyvenv_wheel_dir = meson.project_source_root() + '/python/wheels'


# This group must be available offline
pyvenv_checktests_group = custom_target(
    'pyvenv_checktests_group',
    output: 'checktests.group',
    input: pyvenv_common_deps,
    command: [ python, '@INPUT1@',
               'ensuregroup',
               '--dir', pyvenv_wheel_dir,
               '@INPUT0@',
               'checktests' ],
)

# This group is allowed to fetch from PyPI.
pyvenv_functests_group = custom_target(
    'pyvenv_functests_group',
    output: 'functests.group',
    input: pyvenv_common_deps,
    depends: pyvenv_checktests_group,
    command: [ python, '@INPUT1@',
               'ensuregroup',
               '--online',
               '--dir', pyvenv_wheel_dir,
               '@INPUT0@',
               'functests' ],
)
