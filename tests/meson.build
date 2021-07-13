tests = [
	'unicode.meson',
	'dict.meson',
	'join_paths.meson',
	'katie.meson',
	'join.meson',
]

foreach t : tests
	test(t, muon, args: ['internal', 'eval'] + files([t]))
endforeach
