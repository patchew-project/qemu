qga_qapi_outputs = [
  'qga-qapi-commands.c',
  'qga-qapi-commands.h',
  'qga-qapi-emit-events.c',
  'qga-qapi-emit-events.h',
  'qga-qapi-events.c',
  'qga-qapi-events.h',
  'qga-qapi-init-commands.c',
  'qga-qapi-init-commands.h',
  'qga-qapi-introspect.c',
  'qga-qapi-introspect.h',
  'qga-qapi-types.c',
  'qga-qapi-types.h',
  'qga-qapi-visit.c',
  'qga-qapi-visit.h',
]

qga_qapi_files = custom_target('QGA QAPI files',
                               output: qga_qapi_outputs + ['qga-qapi-doc.texi'],
                               input: 'qapi-schema.json',
                               command: [ qapi_gen, '-o', 'qga', '-p', 'qga-', '@INPUT0@' ],
                               depend_files: qapi_gen_depends)

qga_ss = ss.source_set()
i = 0
foreach output: qga_qapi_outputs
  qga_ss.add(qga_qapi_files[i])
  i = i + 1
endforeach
qga_qapi_doc_texi = qga_qapi_files[i]

qga_ss.add(files(
  'commands.c',
  'guest-agent-command-state.c',
  'main.c',
))
qga_ss.add(when: 'CONFIG_POSIX', if_true: files(
  'channel-posix.c',
  'commands-posix.c'))
qga_ss.add(when: 'CONFIG_WIN32', if_true: files(
  'channel-win32.c',
  'commands-win32.c',
  'service-win32.c',
  'vss-win32.c'
))

qga_ss = qga_ss.apply(config_host, strict: false)

find_program('cargo', required: true)
find_program('rustfmt', required: true)
cargo_sh = find_program('../scripts/cargo.sh', required: true)

run_target('cargo-clippy',
           command: [cargo_sh, 'clippy'])

qga_qapi_rs_outputs = [
  'qga-qapi-sys-types.rs',
  'qga-qapi-types.rs',
  'qga-qapi-dbus.rs',
]

qga_qapi_rs_files = custom_target('QGA QAPI Rust files',
                               output: qga_qapi_rs_outputs,
                               input: 'qapi-schema.json',
                               command: [ qapi_gen, '-r', '-o', 'qga', '-p', 'qga-', '@INPUT0@' ],
                               depend_files: qapi_gen_depends)

rs_files = files(
  'Cargo.toml',
  'error.rs',
  'lib.rs',
  'qapi.rs',
  'qapi_dbus.rs',
  'qapi_sys.rs',
  'qemu.rs',
  'qemu_sys.rs',
  'qmp.rs',
  'translate.rs',
)

buildtype = get_option('buildtype')
cargo_build = custom_target('cargo-build',
                            input: [qga_qapi_rs_files, rs_files],
                            output: ['cargo-build.stamp'],
                            console: true,
                            command: [cargo_sh, 'build', buildtype, meson.current_build_dir(), meson.source_root(), meson.build_root()])
qga_rs = declare_dependency(link_args: ['-lrt', '-ldl', 'rs-target/@0@/libqga.a'.format(buildtype)], sources: cargo_build)

qga = executable('qemu-ga', qga_ss.sources(),
                 link_args: config_host['LIBS_QGA'].split(),
                 dependencies: [qemuutil, libudev, qga_rs],
                 install: true)
all_qga = [qga]

if targetos == 'windows'
  if 'CONFIG_QGA_VSS' in config_host
    subdir('vss-win32')
  else
    gen_tlb = []
  endif

  wixl = find_program('wixl', required: false)
  if wixl.found()
    deps = [gen_tlb, qga]
    if 'CONFIG_QGA_VSS' in config_host and 'QEMU_GA_MSI_WITH_VSS' in config_host
      deps += qga_vss
    endif
    qga_msi = custom_target('QGA MSI',
                            input: files('installer/qemu-ga.wxs'),
                            output: 'qemu-ga-@0@.msi'.format(config_host['ARCH']),
                            depends: deps,
                            command: [
                              'QEMU_GA_VERSION=' + config_host['QEMU_GA_VERSION'],
                              'QEMU_GA_MANUFACTURER=' + config_host['QEMU_GA_MANUFACTURER'],
                              'QEMU_GA_DISTRO=' + config_host['QEMU_GA_DISTRO'],
                              'BUILD_DIR=' + meson.build_root(),
                              wixl, '-o', '@OUTPUT0@', '@INPUT0@',
                              config_host['QEMU_GA_MSI_ARCH'].split(),
                              config_host['QEMU_GA_MSI_WITH_VSS'].split(),
                              config_host['QEMU_GA_MSI_MINGW_DLL_PATH'].split(),
                            ])
    all_qga += [qga_msi]
    alias_target('msi', qga_msi)
  endif
else
  install_subdir('run', install_dir: get_option('localstatedir'))
endif

alias_target('qemu-ga', all_qga)
