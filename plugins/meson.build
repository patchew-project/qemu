specific_ss.add(when: 'CONFIG_PLUGIN', if_true: [files(
  'loader.c',
  'core.c',
  'api.c',
), declare_dependency(link_args: [], compile_args: ['-Wno-missing-prototypes'])])
