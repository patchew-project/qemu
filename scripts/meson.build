if stap.found()
  install_data('qemu-trace-stap', install_dir: get_option('bindir'))
endif

test('xml-preprocess', files('xml-preprocess-test.py'), suite: ['unit'])


pythontests += {
    'dir': meson.current_source_dir() / 'qapi',
    'pylint': false,
    'mypy': true,
    'flake8': false,
    'isort': true,
}
