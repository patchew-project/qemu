system_ss.add([files(
  'video.c',
)])

# index aliases
name  = 0
dep   = 1
files = 2

video_modules = {}
foreach m : [
  ['gstreamer', gstreamer, files('gstreamer.c')],
  ['gstreamer-app', gstreamer_app, files('gstreamer.c')],
  ['libcamera', gstreamer, files('libcamera.c', 'gstreamer.c')],
  ['v4l2', v4l2, files('v4l2.c')],
]
  if m[dep].found()
    module_ss = ss.source_set()
    module_ss.add(m[dep], m[files])
    video_modules += {m[name] : module_ss}
  endif
endforeach

modules += {'video': video_modules}
