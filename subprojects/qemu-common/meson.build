project('qemu-common', 'c',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=gnu11']
)

glib_dep = dependency('glib-2.0')
inc = include_directories('include')

sources = []

subdir('scripts')
subdir('src')

lib = static_library(
  'qemu-common', sources,
  dependencies: [glib_dep],
  include_directories: inc,
)

qemu_common_dep = declare_dependency(
  link_with: lib,
  include_directories: inc,
  dependencies: [glib_dep],
)
