printer_ss.add([files(
  'printer.c',
)])

printer_modules = {}
foreach m : [
  ['builtin', files('builtin.c')],
]
  module_ss = ss.source_set()
  module_ss.add(m[1])
  printer_modules += {m[0] : module_ss}
endforeach

modules += {'printer': printer_modules}
