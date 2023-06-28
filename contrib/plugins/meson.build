project('qemu-plugins', 'c', meson_version: '>=0.50.0')

qemu_src = get_option('qemu_path')
if qemu_src == ''
  qemu_src = '../..'
endif

qemu_include = qemu_src + '/include/qemu'
incdir = include_directories(qemu_include)

plugins = [
  'execlog',
  'hotblocks',
  'hotpages',
  'howvec',
  'lockstep',
  'hwprofile',
  'cache',
  'drcov',
]

th = dependency('threads', required: true)
glib = dependency('glib-2.0', required: true)

foreach p: plugins
  library(p, p + '.c',
    include_directories: incdir,
    dependencies: [th, glib],
    override_options: ['b_lundef=false']
  )
endforeach
