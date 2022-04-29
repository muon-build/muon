project('pathjoin', 'c')

# Test string-args form since that is the canonical way
assert(join_paths('foo') == 'foo', 'Single argument join is broken')
assert(join_paths('foo', 'bar') == 'foo/bar', 'Path joining is broken')
assert(
    join_paths('foo', 'bar', 'baz') == 'foo/bar/baz',
    'Path joining is broken',
)
assert(join_paths('/foo', 'bar') == '/foo/bar', 'Path joining is broken')
assert(join_paths('foo', '/bar') == '/bar', 'Absolute path joining is broken')
assert(join_paths('/foo', '/bar') == '/bar', 'Absolute path joining is broken')
# different than meson: '' is ignored in path joining
# assert(join_paths('/foo', '') == '/foo/', 'Trailing / on path')
assert(join_paths('/foo', '') == '/foo')

# Test array form since people are using that too
assert(join_paths(['foo']) == 'foo', 'Single argument join is broken')
assert(join_paths(['foo', 'bar']) == 'foo/bar', 'Path joining is broken')
assert(
    join_paths(['foo', 'bar', 'baz']) == 'foo/bar/baz',
    'Path joining is broken',
)
assert(join_paths(['/foo', 'bar']) == '/foo/bar', 'Path joining is broken')
assert(join_paths(['foo', '/bar']) == '/bar', 'Absolute path joining is broken')
assert(
    join_paths(['/foo', '/bar']) == '/bar',
    'Absolute path joining is broken',
)
# different than meson: '' is ignored in path joining
# assert(join_paths(['/foo', '']) == '/foo/', 'Trailing / on path')
assert(join_paths(['/foo', '']) == '/foo')

# Division operator should do the same as join_paths
assert('foo' / 'bar' == 'foo/bar', 'Path division is broken')
assert('foo' / 'bar' / 'baz' == 'foo/bar/baz', 'Path division is broken')
assert('/foo' / 'bar' == '/foo/bar', 'Path division is broken')
assert('foo' / '/bar' == '/bar', 'Absolute path division is broken')
assert('/foo' / '/bar' == '/bar', 'Absolute path division is broken')
# different than meson: '' is ignored in path joining
# assert('/foo' / '' == '/foo/', 'Trailing / on path')
assert('/foo' / '' == '/foo')
