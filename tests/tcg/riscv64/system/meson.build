tests = []

# Multi arch tests are not supported
# tests += tcg_tests['multiarch-softmmu']['tests']

link_script = files('../semihost.ld')[0]
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-Wl,-T', link_script]
qemu_args = ['-M', 'virt',
             '-display', 'none',
             '-serial', 'stdio',
             '-semihosting',
             '-bios']

setup = {'cflags': cflags, 'qemu_args': qemu_args}

tests += {
  'doubletrap.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'interruptedmemory.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'issue1060.S': setup,
  'test-mepc-masking.S': setup,
  'test-crc32.S': setup + {'qemu_args': ['-cpu', 'rv64,xlrbr=true', qemu_args]},
  'test-minstret-ecall.S': setup + {'qemu_args': ['-icount', 'shift=1', qemu_args]},
  'test-minstret-wfi.S': setup + {'qemu_args': ['-icount', 'shift=1', qemu_args]},
  'test-misa-w.S': setup + {'qemu_args': ['-cpu', 'rv64,x-misa-w=true,c=true,v=true', qemu_args]},
  'test-zvabd.S': setup + {'qemu_args': ['-cpu', 'rv64,v=true,vlen=256,x-zvabd=true', qemu_args]},
  'test-vsepc-masking.S': setup + {
    'test_name': 'test-vsepc-masking-c',
    'qemu_args': ['-cpu', 'rv64,h=true,c=true', qemu_args],
  },
}

tests += {
  'test-vsepc-masking.S': {
    'test_name': 'test-vsepc-masking-no-c',
    'qemu_args': ['-cpu', 'rv64,h=true,c=false,zca=false', qemu_args],
  },
}

tests += {
  'test-rv128-mstatus.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'x-rv128', qemu_args],
  }
}

tests += {
  'test-rv128-sstatus.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'x-rv128', qemu_args],
  }
}

hyp_guest = files('../hyp-guest.S')[0]

# test-gstage-tinst.S: one build per case; the flags are documented there
gstage_tinst = {
  # the guest's tables are read-only, so the walk's A/D write is refused
  'load':      ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)', '-DREG=3',
                '-DFLAGS=PTE_R', '-DCAUSE=LOAD_GPF'],
  'fetch':     ['-DMODE=VS', '-DACCESS=jr a0', '-DREG=4',
                '-DFLAGS=(PTE_R|PTE_X)', '-DCAUSE=FETCH_GPF'],
  'store':     ['-DMODE=VS', '-DACCESS=sd zero, 0(a0)', '-DREG=5',
                '-DFLAGS=(PTE_R|PTE_W|PTE_A)',
                '-DCAUSE=STORE_GPF'],
  'store-ad':  ['-DMODE=VS', '-DACCESS=sd zero, 0(a0)', '-DREG=6',
                '-DFLAGS=(PTE_R|PTE_W)',
                '-DCAUSE=STORE_GPF'],
  'amo':       ['-DMODE=VS', '-DACCESS=amoadd.d t0, zero, (a0)', '-DREG=7',
                '-DFLAGS=(PTE_R|PTE_W|PTE_A)',
                '-DCAUSE=STORE_GPF'],
  'load-4k':   ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)', '-DREG=8',
                '-DPTE_ADDR=VS_L0', '-DFLAGS=PTE_R',
                '-DCAUSE=LOAD_GPF'],
  'load-1g':   ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)',
                '-DADDR=(0xC0000000+9*0x200000)', '-DPTE_ADDR=(VS_ROOT+3*8)',
                '-DPTE_PA=REGION(0)', '-DFLAGS=PTE_R',
                '-DCAUSE=LOAD_GPF'],
  'load-vu':   ['-DMODE=VU', '-DACCESS=ld t0, 0(a0)', '-DREG=10',
                '-DFLAGS=(PTE_R|PTE_U)', '-DCAUSE=LOAD_GPF'],
  'hlv':       ['-DMODE=HS', '-DACCESS=hlv.d t0, (a0)', '-DREG=11',
                '-DFLAGS=PTE_R', '-DCAUSE=LOAD_GPF'],
  'hsv':       ['-DMODE=HS', '-DACCESS=hsv.d zero, (a0)', '-DREG=12',
                '-DFLAGS=(PTE_R|PTE_W|PTE_A)',
                '-DCAUSE=STORE_GPF'],
  'load-hs':   ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)', '-DREG=13',
                '-DFLAGS=PTE_R', '-DCAUSE=LOAD_GPF',
                '-DTO_HS'],
  'store-hs':  ['-DMODE=VS', '-DACCESS=sd zero, 0(a0)', '-DREG=14',
                '-DFLAGS=(PTE_R|PTE_W|PTE_A)',
                '-DCAUSE=STORE_GPF', '-DTO_HS'],
  # the tables are unreadable, so the walk fails reading root entry 2
  'hsv-noread': ['-DMODE=HS', '-DACCESS=hsv.d zero, (a0)', '-DREG=15',
                 '-DFLAGS=(PTE_R|PTE_W|PTE_A)', '-DPERM=G_NOREAD',
                 '-DENTRY=(VS_ROOT+2*8)', '-DCAUSE=STORE_GPF',
                 '-DTINST=TINST_READ'],
  'hlv-noread': ['-DMODE=HS', '-DACCESS=hlv.d t0, (a0)', '-DREG=16',
                 '-DFLAGS=(PTE_R|PTE_A)', '-DPERM=G_NOREAD',
                 '-DENTRY=(VS_ROOT+2*8)', '-DCAUSE=LOAD_GPF',
                 '-DTINST=TINST_READ'],
  'fetch-noread': ['-DMODE=VS', '-DREG=17', '-DADDR=probe',
                   '-DFLAGS=(PTE_R|PTE_X|PTE_A)', '-DPERM=G_NOREAD',
                   '-DENTRY=(VS_ROOT+2*8)', '-DCAUSE=FETCH_GPF',
                   '-DTINST=TINST_READ'],
  # A/D updating off: an ordinary page fault, nothing written
  'noadue-h':  ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)', '-DREG=21',
                '-DFLAGS=PTE_R', '-DHENVCFG=0', '-DCAUSE=LOAD_PF',
                '-DENTRY=0', '-DTINST_NOT_WALK'],
  'noadue-m':  ['-DMODE=VS', '-DACCESS=ld t0, 0(a0)', '-DREG=22',
                '-DFLAGS=PTE_R', '-DMENVCFG=0', '-DCAUSE=LOAD_PF',
                '-DENTRY=0', '-DTINST_NOT_WALK'],
  'noadue-store': ['-DMODE=VS', '-DACCESS=sd zero, 0(a0)', '-DREG=23',
                   '-DFLAGS=(PTE_R|PTE_W|PTE_A)', '-DHENVCFG=0',
                   '-DCAUSE=STORE_PF', '-DENTRY=0', '-DTINST_NOT_WALK'],
  # control: the guest itself stores into its read-only tables
  'control':   ['-DMODE=VS', '-DACCESS=sd zero, 0(a0)', '-DREG=2',
                '-DFLAGS=(PTE_R|PTE_W|PTE_A|PTE_D)', '-DADDR=(VS_ROOT+0x800)',
                '-DENTRY=(VS_ROOT+0x800)', '-DCAUSE=STORE_GPF',
                '-DTINST_NOT_WALK'],
}
foreach name, flags : gstage_tinst
  tests += {
    'test-gstage-tinst.S': {
      'exe_name': 'test-gstage-tinst-' + name,
      'cflags': cflags + ['-march=rv64gch', hyp_guest] + flags,
      'qemu_args': ['-cpu', 'rv64,h=true,svadu=true', qemu_args],
    }
  }
endforeach

tests += {
  'test-vle32ff.S': {
    'cflags': cflags + ['-march=rv64gcv'],
    'qemu_args': ['-cpu', 'rv64,v=true', qemu_args],
  }
}

tests += {
  'test-zicclsm.S': {
    'cflags': cflags + ['-march=rv64gcv_zfh'],
    'qemu_args': ['-cpu', 'rv64,v=true,zfh=true,zicclsm=true', qemu_args],
  }
}
tests += {
  'test-zicclsm.S': {
    'exe_name': 'test-zicclsm-off',
    'cflags': cflags + ['-march=rv64gcv_zfh', '-DZICCLSM_DISABLED'],
    'qemu_args': ['-cpu', 'rv64,v=true,zfh=true,zicclsm=false', qemu_args],
  }
}

tests += {
  'test-sdext-sigill.S': {
    'exe_name': 'test-sdext-sigill',
    'cflags': cflags + ['-march=rv64gcv'],
    'qemu_args': ['-cpu', 'rv64,sdtrig=true,sdext=true', qemu_args],
  }
}

tests += {
  'test-xsfvqmacc.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'rv64,v=true,vlen=256,xsfvqmaccdod=true,xsfvqmaccqoq=true', qemu_args],
  }
}

if 'qemu-system-riscv64' in emulators
  tcg_tests += {
    'riscv64-softmmu': {
      'cc': 'riscv64-linux-gnu-gcc',
      'cc_alt': ['riscv64-redhat-linux-gnu-gcc', 'riscv64-elf-gcc'],
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'riscv64',
      'gdb_arch': 'riscv64',
      'qemu': emulators['qemu-system-riscv64'],
      'tests': tests,
    }
  }
endif
