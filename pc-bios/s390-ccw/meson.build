project('s390-ccw')

strip = find_program('strip')
cc = meson.get_compiler('c')
link_args = ['-Wl,-pie', '-nostdlib']

# FIXME: find a better way to check/enable slof
slof = '../../roms/SLOF'
has_slof = cc.has_header('libnet/tftp.h', args: '-I' + meson.current_source_dir() / slof / 'lib')

s390_cargs = [
  '-ffreestanding',
  '-fno-delete-null-pointer-checks',
  '-msoft-float',
  '-march=z900',
  '-fPIE',
  '-fno-strict-aliasing',
  '-fno-asynchronous-unwind-tables',
  cc.get_supported_arguments('-fno-stack-protector')
]

s390_incs = []

s390_srcs = [
  'bootmap.c',
  'cio.c',
  'dasd-ipl.c',
  'jump2ipl.c',
  'libc.c',
  'main.c',
  'menu.c',
  'sclp.c',
  'start.S',
  'virtio-blkdev.c',
  'virtio-scsi.c',
  'virtio.c',
]

if has_slof
  s390_srcs += [
    'netmain.c',
    'virtio-net.c',
  ]
  s390_incs += include_directories(slof / 'lib/libnet')
  s390_incs += include_directories(slof / 'lib/libc/include')
  s390_cargs += '-nostdinc'
endif

s390_lib = static_library(
  's390', s390_srcs,
  c_args: s390_cargs,
  include_directories: s390_incs,
)

s390_ccw_elf = executable(
  's390-ccw.elf',
  link_args: link_args,
  objects: s390_lib.extract_objects([
    'bootmap.c',
    'cio.c',
    'dasd-ipl.c',
    'jump2ipl.c',
    'libc.c',
    'main.c',
    'menu.c',
    'sclp.c',
    'start.S',
    'virtio-blkdev.c',
    'virtio-scsi.c',
    'virtio.c',
  ]),
)

custom_target(
  's390-ccw.img',
  output: 's390-ccw.img',
  input: s390_ccw_elf,
  command: [strip, '--strip-unneeded', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true,
)

if has_slof
  slof_ctype = slof / 'lib/libc/ctype'
  slof_str = slof / 'lib/libc/string'
  slof_stdlib = slof / 'lib/libc/stdlib'
  slof_stdio = slof / 'lib/libc/stdio'

  s390_libc = static_library(
    's390-libc', files(
      slof_ctype / 'isdigit.c',
      slof_ctype / 'isxdigit.c',
      slof_ctype / 'toupper.c',
      slof_str / 'strcat.c',
      slof_str / 'strchr.c',
      slof_str / 'strrchr.c',
      slof_str / 'strcpy.c',
      slof_str / 'strlen.c',
      slof_str / 'strncpy.c',
      slof_str / 'strcmp.c',
      slof_str / 'strncmp.c',
      slof_str / 'strcasecmp.c',
      slof_str / 'strncasecmp.c',
      slof_str / 'strstr.c',
      slof_str / 'memset.c',
      slof_str / 'memcpy.c',
      slof_str / 'memmove.c',
      slof_str / 'memcmp.c',
      slof_stdlib / 'atoi.c',
      slof_stdlib / 'atol.c',
      slof_stdlib / 'strtoul.c',
      slof_stdlib / 'strtol.c',
      slof_stdlib / 'rand.c',
      slof_stdlib / 'malloc.c',
      slof_stdlib / 'free.c',
      slof_stdio / 'sprintf.c',
      slof_stdio / 'snprintf.c',
      slof_stdio / 'vfprintf.c',
      slof_stdio / 'vsnprintf.c',
      slof_stdio / 'vsprintf.c',
      slof_stdio / 'fprintf.c',
      slof_stdio / 'printf.c',
      slof_stdio / 'putc.c',
      slof_stdio / 'puts.c',
      slof_stdio / 'putchar.c',
      slof_stdio / 'stdchnls.c',
      slof_stdio / 'fileno.c',
      slof / 'slof/sbrk.c',
    ),
    c_args: s390_cargs,
    include_directories: s390_incs,
  )

  slof_libnet = slof / 'lib/libnet'
  s390_libnet = static_library(
    's390-libnet', files(
      slof_libnet / 'args.c',
      slof_libnet / 'dhcp.c',
      slof_libnet / 'dns.c',
      slof_libnet / 'icmpv6.c',
      slof_libnet / 'ipv6.c',
      slof_libnet / 'tcp.c',
      slof_libnet / 'udp.c',
      slof_libnet / 'bootp.c',
      slof_libnet / 'dhcpv6.c',
      slof_libnet / 'ethernet.c',
      slof_libnet / 'ipv4.c',
      slof_libnet / 'ndp.c',
      slof_libnet / 'tftp.c',
      slof_libnet / 'pxelinux.c',
    ),
    c_args: [s390_cargs, '-DDHCPARCH=0x1F'],
    include_directories: s390_incs,
  )

  s390_netboot_elf = executable(
    's390-netboot.elf',
    link_with: [s390_libc, s390_libnet],
    link_args: [link_args, '-Ttext=0x7800000'],
    objects: s390_lib.extract_objects([
      'cio.c',
      'jump2ipl.c',
      'netmain.c',
      'sclp.c',
      'start.S',
      'virtio-net.c',
      'virtio.c',
    ]),
  )

  custom_target(
    's390-netboot.img',
    output: 's390-netboot.img',
    input: s390_netboot_elf,
    command: [strip, '--strip-unneeded', '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true,
  )
endif
