test_files = ['smbios.py', 'smilatency.py', 'testacpi.py', 'testcpuid.py']

copytestfiles = custom_target('copy test files',
  input : test_files,
  output :  test_files,
  command : ['cp', '@INPUT@', '@OUTDIR@'],
  install : true,
  install_dir : 'bits-tests',
  build_by_default : true)

other_deps += copytestfiles
