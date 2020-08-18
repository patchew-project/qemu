if 'CONFIG_PVRDMA' in config_host
  # if not found, CONFIG_PVRDMA should not be set
  libumad = cc.find_library('ibumad', required: true)
  executable('rdmacm-mux', files('main.c'),
             dependencies: [glib, libumad],
             build_by_default: targetos == 'linux',
             install: false)
endif
