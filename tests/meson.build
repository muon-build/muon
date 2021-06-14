tests = [
	'test.meson',
	'dict.meson',
	'join_paths.meson',
]

foreach t : tests
	test(t, muon, args: ['eval'] + files([t]))
endforeach
