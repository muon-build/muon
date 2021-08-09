tests = [
	{ 'name': 'unicode.meson', },
	{ 'name': 'badnum.meson', 'should_fail': true },
	{ 'name': 'dict.meson', },
	{ 'name': 'join_paths.meson', },
	{ 'name': 'katie.meson', },
	{ 'name': 'join.meson', },
	{ 'name': 'version_compare.meson', },
	{ 'name': 'multiline.meson', },
	{ 'name': 'ternary.meson', },
]

foreach t : tests
	if 'should_fail' in t
		should_fail = t['should_fail']
	else
		should_fail = false
	endif

	test(
		t['name'],
		muon,
		args: ['internal', 'eval'] + files([t['name']]),
		should_fail: should_fail
	)
endforeach
