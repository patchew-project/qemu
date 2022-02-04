qom_ss.add(genh)
qom_ss.add(files(
  'container.c',
  'object.c',
  'qom-qobject.c',
))

if have_system or have_tools
  qom_ss.add(files('object_interfaces.c'))
else
  qom_ss.add(files('user_creatable-stub.c'))
endif

qmp_ss.add(files('qom-qmp-cmds.c'))
softmmu_ss.add(files('qom-hmp-cmds.c'))
