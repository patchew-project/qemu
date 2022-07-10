xorriso = find_program('xorriso', required: true)
if not xorriso.found()
  message('xorriso not found ... disabled bits acpi tests.')
  subdir_done()
endif

subdir('bits-tests')
subdir('bits-config')

test_files = ['acpi-bits-test.py']
requirements = 'requirements.txt'

copytestfiles = custom_target('copy test files',
  input : test_files,
  output :  test_files,
  command : ['cp', '@INPUT@', '@OUTPUT@'],
  install : false,
  build_by_default : true)

requirementsfiles = custom_target('copy py req files',
  input : requirements,
  output : requirements,
  command : ['cp', '@INPUT@', '@OUTPUT@'],
  install : false,
  build_by_default : true)

other_deps += [copytestfiles,requirementsfiles]

pytest_executables += {
    'acpi-bits-test': configure_file(copy:true,
                                     input:'acpi-bits-test-venv.sh',
				     output:'acpi-bits-test')
}
