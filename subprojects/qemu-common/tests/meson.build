env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
]

test('error-report',
  executable('test-error-report',
    sources: files('test-error-report.c'),
    dependencies: qemu_common_dep,
  ),
  env: env,
)
