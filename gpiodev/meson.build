gpiodev_ss.add(files(
  'gpio-chardev.c',
  'gpio-fe.c',
  'gpio.c',
))

gpiodev_ss.add(when: fuse, if_true: files('gpio-guse.c'))
gpiodev_ss = gpiodev_ss.apply({})
