fs = import('fs')

sources = {
    'scalar.c' : ['--translate-all-helpers'],
    'ldst.c' : [
        '--translate-all-helpers',
        '--mmu-index-function=tb_mmu_index',
    ],
    'cpustate.c' : [
        '--translate-all-helpers',
        '--tcg-global-mappings=mappings',
    ],
    'vector.c' : [
        '--translate-all-helpers',
        '--temp-vector-block=vmem',
    ],
    'call-translation.c' : [],
    'call-to-declaration.c' : [
        '--allow-decl-call',
    ],
    'forward-context.c' : [
        '--allow-decl-call',
        '--forward-context',
    ],
    'user-pcrel-jump.c' : [
        '--allow-decl-call',
        '--user-pcrel-branch-func=target_pcrel_jump',
        '--user-pcrel-branch-conditional-func=target_pcrel_jump_cond',
        '--user-pcrel-branch-fallthrough-func=target_pcrel_jump_fall',
    ],
}

sources_vector_layout = {
    'vector-layout.c' : [],
}

diff = find_program('diff')

foreach file : sources.keys()
    extra_flags = sources.get(file)

    name = file.split('.')[0]
    name_ll = name + '.ll'

    ll = custom_target(name_ll,
        input: files(file),
        output: name_ll,
        command: [clang, '-O0', '-Xclang', '-disable-O0-optnone',
                  '-g', '-S', '-emit-llvm', '-o', '@OUTPUT@', '@INPUT@']
    )

    output_prefix = join_paths(meson.current_build_dir(), name)
    flags = [ll,
        '--output-enabled=' + output_prefix + '-enabled',
        '--output-source=' + output_prefix + '.c',
        '--output-header=' + output_prefix + '.h',
        '--mmu-index-function=tb_mmu_index',
        '--tcg-global-mappings=mappings']
    flags += extra_flags

    runtest = name + '-run-' + version_major.to_string()
    test(runtest,
        pipeline,
        depends: ll,
        priority: 2,
        args: flags,
        suite: 'helper-to-tcg')

    ref = join_paths(meson.current_source_dir(), 'ref', version_major.to_string(), name + '.c')
    if fs.exists(ref)
        test(name + '-diff-' + version_major.to_string(),
            diff,
            priority: 1,
            args: ['-u', output_prefix + '.c', ref ], suite: 'helper-to-tcg')
    endif
endforeach

# Build and compare tests against references again, but force different vector layouts.
foreach file : sources_vector_layout.keys()
    extra_flags = sources_vector_layout.get(file)
    name = file.split('.')[0]
    name_ll = name + '.ll'

    ll = custom_target(name_ll,
        input: files(file),
        output: name_ll,
        command: [clang, '-O0', '-Xclang', '-disable-O0-optnone',
                  '-g', '-S', '-emit-llvm', '-o', '@OUTPUT@', '@INPUT@']
    )

    foreach size : [8,16,32,64]
        foreach lane0 : ['LeastSignificant', 'MostSignificant']
            foreach big_endian: ['true', 'false']
                test_str = big_endian[0] + size.to_string() + lane0[0]
                output_prefix = join_paths(meson.current_build_dir(), name + '-' + test_str)
                runtest = name + '-run-' + test_str
                test(runtest,
                    pipeline,
                    depends: ll,
                    priority: 2,
                    args: [ll,
                           '--output-enabled=' + output_prefix + '-enabled',
                           '--output-source=' + output_prefix + '.c',
                           '--output-header=' + output_prefix + '.h',
                           '--translate-all-helpers',
                           '--vec-force-big-endian=' + big_endian,
                           '--vec-block-bytes=' + size.to_string(),
                           '--vec-lane-0=' + lane0,
                    ] + extra_flags,
                    suite: 'helper-to-tcg')
                ref = join_paths(meson.current_source_dir(), 'ref', version_major.to_string(), name + '-' + test_str + '.c')
                if fs.exists(ref)
                    test(name + '-diff-' + test_str,
                        diff,
                        priority: 1,
                        args: ['-u', output_prefix + '.c', ref ], suite: 'helper-to-tcg')
                endif
            endforeach
        endforeach
    endforeach
endforeach
