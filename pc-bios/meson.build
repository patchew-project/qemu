roms = []
if unpack_edk2_blobs

  fds = []
  if 'aarch64-softmmu' in target_dirs
    fds += [ 'edk2-aarch64-code.fd' ]
  endif

  if 'arm-softmmu' in target_dirs or 'aarch64-softmmu' in target_dirs
    fds += [ 'edk2-arm-code.fd',
             'edk2-arm-vars.fd' ]
  endif

  if 'riscv64-softmmu' in target_dirs
    fds += [ 'edk2-riscv-code.fd',
             'edk2-riscv-vars.fd' ]
  endif

  if 'i386-softmmu' in target_dirs
    fds += [ 'edk2-i386-code.fd',
             'edk2-i386-secure-code.fd',
             'edk2-i386-vars.fd' ]
  endif

  if 'x86_64-softmmu' in target_dirs
    fds += [ 'edk2-x86_64-code.fd',
             'edk2-x86_64-secure-code.fd' ]
  endif

  if 'loongarch64' in target_dirs
    fds += [ 'edk2-loongarch64-code.fd',
             'edk2-loongarch64-vars.fd' ]
  endif

  foreach f : fds
    roms += custom_target(f,
                  build_by_default: have_system,
                  output: f,
                  input: files('@0@.bz2'.format(f)),
                  capture: true,
                  install: get_option('install_blobs'),
                  install_dir: qemu_datadir,
                  command: [ bzip2, '-dc', '@INPUT0@' ])
  endforeach
endif

blobs = []

if 'aarch64-softmmu' in target_dirs
  blobs += [ 'ast27x0_bootrom.bin',
             'npcm7xx_bootrom.bin',
             'npcm8xx_bootrom.bin' ]
endif

# Most x86 blobs start in real mode anyway, need to check which blobs
# are x86_64 only. Also we could limit the option roms based on the
# build config.
if 'x86_64-softmmu' in target_dirs or 'i386-softmmu' in target_dirs
  blobs += [ 'bios.bin',
             'bios-256k.bin',
             'bios-microvm.bin',
             'qboot.rom',
             'vgabios.bin',
             'vgabios-cirrus.bin',
             'vgabios-stdvga.bin',
             'vgabios-vmware.bin',
             'vgabios-qxl.bin',
             'vgabios-virtio.bin',
             'vgabios-ramfb.bin',
             'vgabios-bochs-display.bin',
             'vgabios-ati.bin',
             'pxe-e1000.rom',
             'pxe-eepro100.rom',
             'pxe-ne2k_pci.rom',
             'pxe-pcnet.rom',
             'pxe-rtl8139.rom',
             'pxe-virtio.rom',
             'efi-e1000.rom',
             'efi-eepro100.rom',
             'efi-ne2k_pci.rom',
             'efi-pcnet.rom',
             'efi-rtl8139.rom',
             'efi-virtio.rom',
             'efi-e1000e.rom',
             'efi-vmxnet3.rom',
             'multiboot.bin',
             'multiboot_dma.bin',
             'linuxboot.bin',
             'linuxboot_dma.bin',
             'kvmvapic.bin',
             'pvh.bin' ]
endif

if 'sparc32-softmmu' in target_dirs
  blobs += [ 'openbios-sparc32',
             'QEMU,tcx.bin',
             'QEMU,cgthree.bin' ]
endif

if 'sparc64-softmmu' in target_dirs
  blobs += [ 'openbios-sparc64' ]
endif

if 'ppc64-softmmu' in target_dirs
  blobs += [ 'openbios-ppc',
             'slof.bin',
             'skiboot.lid',
             'pnv-pnor.bin',
             'u-boot.e500',
             'u-boot-sam460-20100605.bin',
             'vof.bin',
             'vof-nvram.bin' ]
endif

if 'ppc32-softmmu' in target_dirs
  blobs += [ 'qemu_vga.ndrv' ]
endif

if host_os == 'windows'
  blobs += [ 'qemu-nsis.bmp' ]
endif

if 's390x-softmmu' in target_dirs
  blobs += [ 's390-ccw.img' ]
endif

if 'alpha-softmmu' in target_dirs
  blobs += [ 'palcode-clipper' ]
endif

if 'hppa-softmmu' in target_dirs
  blobs += [ 'hppa-firmware.img',
             'hppa-firmware64.img' ]
endif

if 'riscv32-softmmu' in target_dirs
  blobs += [ 'opensbi-riscv32-generic-fw_dynamic.bin' ]
endif

if 'riscv64-softmmu' in target_dirs
  blobs += [ 'opensbi-riscv64-generic-fw_dynamic.bin' ]
endif

blobs += [ 'edk2-licenses.txt' ]

if get_option('install_blobs')
  install_data(blobs, install_dir: qemu_datadir, install_mode: 'rw-r--r--')
endif

subdir('descriptors')
subdir('dtb')
subdir('keymaps')
