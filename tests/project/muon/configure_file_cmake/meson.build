# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('configure_file_cmake')

fs = import('fs')

conf_data = configuration_data()
conf_data.set10('foo', true)
conf_data.set('HAVE_FOO', 'foo_content')

conf_data.set10('trueval', true)
conf_data.set10('falseval', false)
conf_data.set('zeroval', 0)
conf_data.set('oneval', 1)
conf_data.set('tenval', 10)
conf_data.set('stringval', 'test')

conf_data.set10('booltrueval', true)
conf_data.set10('boolfalseval', false)
conf_data.set('boolzeroval', 0)
conf_data.set('booloneval', 1)
conf_data.set('booltenval', 10)
conf_data.set('boolstringval', 'test')

conf_data.set('DOLLAR', '$')
conf_data.set('TEXT', 'TRAP')
conf_data.set('STRING', 'TEXT')
conf_data.set('STRING_AT', '@STRING@')
conf_data.set('STRING_CURLY', '{STRING}')
conf_data.set('STRING_VAR', '${STRING}')

conf_data.set('UNDERSCORE', '_')
conf_data.set('NEST_UNDERSCORE_PROXY', 'UNDERSCORE')
conf_data.set('NEST_PROXY', 'NEST_UNDERSCORE_PROXY')

configure_file(
    input: 'config.h.in',
    output: 'config.h',
    format: 'cmake',
    configuration: conf_data,
)

output = fs.read('@0@/config.h'.format(meson.current_build_dir())).split('\n')
expected_output = fs.read('expected_config.h').split('\n')

if output.length() != expected_output.length()
    message(
        'Output is of size @0@ but expected @1@'.format(output.length(), expected_output.length()),
    )
    assert(false)
endif

foreach i : range(output.length())
    eq = output[i] == expected_output[i]
    if not eq
        p('-----------------\nactual: ', pretty: true)
        p(output[i], pretty: true)
        p('!=\nexpected: ', pretty: true)
        p(expected_output[i], pretty: true)
        p('-----------------\n', pretty: true)
    endif
    assert(eq)
endforeach
