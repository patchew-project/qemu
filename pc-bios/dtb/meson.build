dts_sources = [
  'bamboo.dts',
  'canyonlands.dts',
  'petalogix-s3adsp1800.dts',
  'petalogix-ml605.dts',
]

if dtc.found()
  foreach f : dts_sources
    out = fs.replace_suffix(f, '.dtb')
    custom_target(out,
        build_by_default: have_system,
        input: files(f),
        output: out,
        install: get_option('install_blobs'),
        install_dir: qemu_datadir / 'dtb',
        command: [ dtc, '-q', '-I', 'dts', '-O', 'dtb',
                        '-o', '@OUTPUT@', '@INPUT0@' ])
  endforeach
endif
