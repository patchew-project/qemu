system_ss.add(when: 'CONFIG_TCG', if_true: files(
  'replay.c',
  'replay-internal.c',
  'replay-events.c',
  'replay-time.c',
  'replay-input.c',
  'replay-char.c',
  'replay-snapshot.c',
  'replay-net.c',
  'replay-random.c',
  'replay-debugging.c',
), if_false: files('stubs-system.c'))

if have_audio
  system_ss.add(when: 'CONFIG_TCG',
  if_true: files('replay-audio.c'),
  if_false: files('replay-audio-stub.c'))
else
  system_ss.add(files('replay-audio-stub.c'))
endif
