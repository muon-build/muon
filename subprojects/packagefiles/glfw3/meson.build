project(
	'glfw',
	'c',
	version: '3.3.0',
	license: 'zlib',
	default_options: ['default_library=static'],
)

cc = meson.get_compiler('c')
compiler_id = cc.get_id()

math_dep = cc.find_library('m', required: false)
threads_dep = dependency('threads')

if compiler_id == 'msvc'
	if get_option('glfw_use_msvc_runtime_library_dll')
		warning('glfw_use_msvc_runtime_library_dll is not implemented')
		# TODO
	endif
endif

host_system = host_machine.system()

backend = get_option('glfw_backend')
if backend == 'auto'
	if host_system == 'windows' or host_system == 'cygwin'
		backend = 'win32'
	elif host_system == 'darwin'
		backend = 'cocoa'
	else
		error('Unable to determine the backend automatically.')
	endif
endif


subdir('src')
