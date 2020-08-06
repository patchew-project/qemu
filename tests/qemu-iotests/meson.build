dep = [qemu_img, qemu_io, qemu_nbd, emulators]
if 'CONFIG_LINUX' in config_host
    dep += executable('socket_scm_helper', 'socket_scm_helper.c',
                      build_by_default: false)
endif
test('qemu-iotests', sh, args: [files('../check-block.sh')], depends: dep,
     suite: 'block', timeout: 10000)

