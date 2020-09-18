project('optionrom')

cc = meson.get_compiler('c')
objcopy = find_program('objcopy')
signrom = find_program(meson.current_source_dir() / '../../scripts/signrom.py')

emu = ''
foreach e: ['elf_i386', 'elf_i386_fbsd', 'elf_i386_obsd', 'i386pe']
  if cc.has_multi_link_arguments('-nostdlib', '-Wl,-emain', '-m32', '-Wl,-m' + e)
    emu = e
    break
  endif
endforeach

if emu == ''
  message('No suitable compiler/linker found to build optionrom')
  subdir_done()
endif

link_args = ['-nostdlib', '-m32', '-Wl,-m' + e]
link_args += cc.get_supported_link_arguments('-Wl,--build-id=none')
if cc.has_multi_link_arguments('-fno-pie', '-no-pie')
  link_args += ['-no-pie']
endif

link_args += '-Wl,-T' + meson.current_source_dir() / 'flat.lds'

c_args = ['-O2', '-ffreestanding', '-march=i486']
c_args += cc.get_supported_arguments('-fno-pie', '-fno-stack-protector', '-Wa,-32')
if cc.has_argument('-m16')
  c_args += '-m16'
else
  # Attempt to work around compilers that lack -m16 (GCC <= 4.8, clang <= ??)
  # On GCC we add -fno-toplevel-reorder to keep the order of asm blocks with
  # respect to the rest of the code.  clang does not have -fno-toplevel-reorder,
  # but it places all asm blocks at the beginning and we're relying on it for
  # the option ROM header.  So just force clang not to use the integrated
  # assembler, which doesn't support .code16gcc.
  c_args += cc.get_supported_arguments('-fno-toplevel-reorder', '-no-integrated-as')
  c_args += ['-m32', '-include', meson.current_source_dir() / 'code16gcc.h']
endif

foreach target, src: {
  'multiboot': ['multiboot.S'],
  'linuxboot_dma': ['linuxboot_dma.c'],
  'linuxboot': ['linuxboot.S'],
  'kvmvapic': ['kvmvapic.S'],
  'pvh': ['pvh.S', 'pvh_main.c'],
 }
  img = executable(
    target + '.img',
    src,
    c_args: c_args,
    include_directories: include_directories('../../include'),
    link_args: link_args,
    override_options: ['b_coverage=false'],
  )

  raw = custom_target(
    target + '.raw',
    output: target + '.raw',
    input: img,
    command: [objcopy, '-O', 'binary', '-j', '.text', '@INPUT@', '@OUTPUT@'],
  )

  bin = custom_target(
    target + '.bin',
    output: target + '.bin',
    input: raw,
    command: [signrom, '@INPUT@', '@OUTPUT@'],
    build_by_default: true,
  )
endforeach
