project('qemu', ['c', 'cpp'], meson_version: '>=0.55.0',
        default_options: ['warning_level=1', 'c_std=gnu99', 'cpp_std=gnu++03', 'b_lundef=false'],
        version: run_command('head', meson.source_root() / 'VERSION').stdout().strip())

not_found = dependency('', required: false)
keyval = import('unstable-keyval')
config_host = keyval.load(meson.current_build_dir() / 'config-host.mak')

add_project_arguments(config_host['QEMU_CFLAGS'].split(),
                      language: ['c', 'objc'])
add_project_arguments(config_host['QEMU_CXXFLAGS'].split(),
                      language: 'cpp')
add_project_link_arguments(config_host['QEMU_LDFLAGS'].split(),
                           language: ['c', 'cpp', 'objc'])
add_project_arguments(config_host['QEMU_INCLUDES'].split(),
                      language: ['c', 'cpp', 'objc'])

configure_file(input: files('scripts/ninjatool.py'),
               output: 'ninjatool',
               configuration: config_host)
