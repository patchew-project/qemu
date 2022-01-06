camera_ss.add([files(
  'camera.c',
)])

camera_modules = {}
foreach m : [
  ['builtin', cairo, files('builtin.c')],
]
  if m[1].found()
    module_ss = ss.source_set()
    module_ss.add(m[1], m[2])
    camera_modules += {m[0] : module_ss}
  endif
endforeach

modules += {'camera': camera_modules}
