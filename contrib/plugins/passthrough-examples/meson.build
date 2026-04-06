# SPDX-License-Identifier: GPL-2.0-or-later

passthrough_examples = []

if passthrough_host_supported and get_option('plugins')
  libz_htl = shared_library(
    'z_HTL',
    files('zlib-htl.c'),
    install: false,
    dependencies: [zlib],
  )
  passthrough_examples += [libz_htl]
endif

if passthrough_examples.length() > 0
  alias_target('contrib-passthrough-examples', passthrough_examples)
else
  run_target('contrib-passthrough-examples', command: [python, '-c', ''])
endif
