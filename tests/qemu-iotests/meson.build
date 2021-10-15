if have_tools
  qemu_iotests_binaries = [qemu_img, qemu_io, qemu_nbd, qsd]
  foreach k, v : emulators
    if k.startswith('qemu-system-')
      qemu_iotests_binaries += v
    endif
  endforeach
  test('qemu-iotests', sh, args: [files('../check-block.sh')],
       depends: qemu_iotests_binaries,
       protocol: 'tap',
       suite: 'block',
       timeout: 0,
       is_parallel: false)
endif
