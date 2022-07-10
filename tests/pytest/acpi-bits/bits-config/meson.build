config_files = ['bits-cfg.txt']

copycfgfiles = custom_target('copy cfg files',
  input : config_files,
  output :  config_files,
  command : ['cp', '@INPUT@', '@OUTPUT@'],
  install : true,
  install_dir : 'bits-config',
  build_by_default : true)

other_deps += copycfgfiles
