# different from meson: no global compiler cache so we have to add cpp here
# since it is the link lang of foo
project('link to extracted objects', 'c', 'cpp')

sublib = subproject('myobjects').get_variable('sublib')

mainlib = static_library('foo', 'foo.c', install: true, link_with: sublib)
