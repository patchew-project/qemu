modinfo_dep = not_found
if enable_modules
    modinfo_src = custom_target('modinfo.c',
                                output: 'modinfo.c',
                                input: audio_modinfo_files,
                                command: [modinfo_generate, '--skip-missing-deps', '@INPUT@'],
                                capture: true)

    modinfo_lib = static_library('modinfo.c', modinfo_src)
    modinfo_dep = declare_dependency(link_with: modinfo_lib)
endif

# manual audio test - not part of automated test suite
# as it relies on audio system
executable('test-audio',
    sources: files('test-audio.c', 'audio-stubs.c'),
    dependencies: [audio, qemuutil, spice, modinfo_dep],
    build_by_default: false)
