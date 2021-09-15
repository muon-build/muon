tests = [
	['unicode.meson'],
	['badnum.meson', {'should_fail': true }],
	['dict.meson'],
	['join_paths.meson'],
	['katie.meson'],
	['join.meson'],
	['version_compare.meson'],
	['multiline.meson'],
	['ternary.meson'],
	['fstring.meson'],
	['array.meson'],
	['environment.meson', {'env': 'inherited=secert' }],
]

foreach t : tests

	if t.length() == 2
		kwargs = t[1]
	else
		kwargs = {}
	endif

	args = ['internal', 'eval'] + files([t[0]])

	test(t[0], muon, args: args, kwargs: kwargs)
endforeach
