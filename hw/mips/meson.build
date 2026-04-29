mips_ss = ss.source_set()
mips_ss.add(files('bootloader.c', 'mips_int.c'))
common_ss.add(when: 'CONFIG_FW_CFG_MIPS', if_true: files('fw_cfg.c'))
common_ss.add(when: 'CONFIG_LOONGSON3V', if_true: files('loongson3_bootp.c'))
mips_ss.add(when: 'CONFIG_LOONGSON3V', if_true: files('loongson3_virt.c'))
mips_ss.add(when: 'CONFIG_MALTA', if_true: files('malta.c'))
mips_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('cps.c'))

if 'CONFIG_TCG' in config_all_accel
mips_ss.add(when: 'CONFIG_JAZZ', if_true: files('jazz.c'))
mips_ss.add(when: 'CONFIG_FULOONG', if_true: files('fuloong2e.c'))
mips_ss.add(when: 'CONFIG_MIPS_BOSTON', if_true: files('boston.c'))
endif

mips_ss.add(when: 'CONFIG_PIC32MK', if_true: files(
  'pic32mk.c',
  'pic32mk_evic.c',
  'pic32mk_uart.c',
  'pic32mk_timer.c',
  'pic32mk_gpio.c',
  'pic32mk_spi.c',
  'pic32mk_i2c.c',
  'pic32mk_dma.c',
  'pic32mk_canfd.c',
  'pic32mk_usb.c',
  'pic32mk_wdt.c',
  'pic32mk_cru.c',
  'pic32mk_cfg.c',
  'pic32mk_adchs.c',
  'pic32mk_dataee.c',
  'pic32mk_nvm.c',
  'pic32mk_oc.c',
  'pic32mk_ic.c',
))

hw_arch += {'mips': mips_ss}
