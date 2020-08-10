softmmu_ss.add(files(
  'audio.c',
  'audio_legacy.c',
  'mixeng.c',
  'noaudio.c',
  'wavaudio.c',
  'wavcapture.c',
))

softmmu_ss.add(when: [spice, 'CONFIG_SPICE'], if_true: files('spiceaudio.c'))
softmmu_ss.add(when: [coreaudio, 'CONFIG_AUDIO_COREAUDIO'], if_true: files('coreaudio.c'))
softmmu_ss.add(when: [dsound, 'CONFIG_AUDIO_DSOUND'], if_true: files('dsoundaudio.c'))
softmmu_ss.add(when: ['CONFIG_AUDIO_WIN_INT'], if_true: files('audio_win_int.c'))

audio_modules = []
if 'CONFIG_AUDIO_ALSA' in config_host
  audio_modules += [['alsa', files('alsaaudio.c'), alsa]]
endif
if 'CONFIG_AUDIO_OSS' in config_host
  audio_modules += [['oss', files('ossaudio.c'), oss]]
endif
if 'CONFIG_AUDIO_PA' in config_host
  audio_modules += [['pa', files('paaudio.c'), pulse]]
endif
if 'CONFIG_AUDIO_SDL' in config_host
  audio_modules += [['sdl', files('sdlaudio.c'), sdl]]
endif
if 'CONFIG_AUDIO_JACK' in config_host
  audio_modules += [['jack', files('jackaudio.c'), jack]]
endif

modules += {'audio': audio_modules}
