# Note that this file only controls "optional" dependency groups; groups
# *required* for the build are handled directly in configure instead:
# namely, meson and sphinx.

# NB: This command may or may not include the "--online" flag, depending
# on the results of configure.
ensuregroup_cmd = config_host['MKVENV_ENSUREGROUP'].split()

pyvenv_common_deps = files(
    meson.project_source_root() + '/pythondeps.toml',
    meson.project_source_root() + '/python/scripts/mkvenv.py'
)
pyvenv_wheel_dir = meson.project_source_root() + '/python/wheels'


# This group is allowed to rely on internet, to fetch from PyPI.
# If --online was not passed to configure, this could fail.
pyvenv_functests_group = custom_target(
    'pyvenv_functests_group',
    output: 'functests.group',
    input: pyvenv_common_deps,
    command: ensuregroup_cmd + [
        '--online',
        '--dir', pyvenv_wheel_dir,
        '@INPUT0@',
        'functests',
    ],
)
