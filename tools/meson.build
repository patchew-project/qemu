have_virtiofsd = (have_system and
    'CONFIG_LINUX' in config_host and 
    'CONFIG_SECCOMP' in config_host and
    'CONFIG_LIBCAP_NG' in config_host)
if have_virtiofsd
  subdir('virtiofsd')
endif
