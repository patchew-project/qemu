subdir('qemu-api-macros')
subdir('qemu-api')

subdir('hw')

cargo = find_program('cargo', required: false)

if cargo.found()
  run_target('rustfmt',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'fmt'],
    depends: bindings_rs)
endif
