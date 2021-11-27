if not have_bsd_user
   subdir_done()
endif

smoke_bsd_user = find_program('smoke-bsd-user')

bsd_user_archs = [ 'armv7', 'amd64', 'i386' ]
targs = []
foreach i : bsd_user_archs
    h = 'h.' + i
    targs += custom_target('bsd-user h.' + i,
        output : h,
	input : h + '.S',
	command : ['clang',
	    '-target',
	    i + '-unknown-freebsd14.0',
	    '-o',
	    '@OUTPUT@',
	    '@INPUT@',
	    '-nostdlib',
	    '-Wl,-e',
	    '-Wl,qemu_start',
	    '-static'],
	install : false)
endforeach

test('bsd-user-smoke', smoke_bsd_user,
    args: [meson.project_build_root(), meson.current_build_dir()],
    suite: 'smoke',
    depends: targs
)
