# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

test(
    'test_all_meson_files',
    muon,
    args: [
        'internal',
        'eval',
        meson.current_source_dir() / 'test_all_meson_files.meson',
        muon,
        meson.project_source_root(),
    ],
    suite: 'fmt',

    # Increased timeout necessary to avoid sporadic timeouts on GitHub ubuntu workflow
    timeout: 90,
)

subdir('editorconfig')

foreach case : [
    {'case': 'space_array'},
    {'case': 'kwargs_force_multiline'},
    {'case': 'wide_colon'},
    {'case': 'no_single_comma_function'},
    {'case': 'indent_before_comments'},
    {
        'case': 'insert_final_newline_false',
        'input': 'final_newline_present',
        'expect': 'final_newline_missing',
    },
    {
        'case': 'insert_final_newline_true',
        'input': 'final_newline_missing',
        'expect': 'final_newline_present',
    },
    {
        'case': 'sort_files',
        'input': 'files_unsorted',
        'expect': 'files_sorted',
    },
    {'case': 'group_arg_value'},
    {
        'case': 'simplify_string_literals',
        'input': 'simplify_string_literals_in',
        'expect': 'simplify_string_literals_out',
    },
    {
        'case': 'end_of_line_lf_to_crlf',
        'conf': 'end_of_line_lf',
        'input': 'end_of_line_crlf',
        'expect': 'end_of_line_lf',
    },
    {
        'case': 'end_of_line_crlf_to_lf',
        'conf': 'end_of_line_crlf',
        'input': 'end_of_line_lf',
        'expect': 'end_of_line_crlf',
    },
    {
        'case': 'end_of_line_auto_crlf',
        'conf': 'end_of_line_auto',
        'input': 'end_of_line_mixed_crlf_first',
        'expect': 'end_of_line_crlf',
    },
    {
        'case': 'end_of_line_auto_lf',
        'conf': 'end_of_line_auto',
        'input': 'end_of_line_mixed_lf_first',
        'expect': 'end_of_line_lf',
    },
]
    path_base = meson.current_source_dir() / 'cases'
    case_name = case['case']
    test_name = 'case' / case_name
    conf = path_base / ('conf' in case ? case['conf'] : case_name) + '.ini'

    if 'expect' in case
        input = case['input']
        test(
            test_name,
            muon,
            args: [
                'internal',
                'eval',
                meson.current_source_dir() / 'fmt_expect.meson',
                muon,
                conf,
                f'@path_base@/@input@.meson',
                meson.current_source_dir() / 'cases' / (case['expect'] + '.meson'),
            ],
            suite: 'fmt',
        )
    else
        test(
            test_name,
            muon,
            args: [
                'fmt',
                '-eq',
                '-c', conf,
                f'@path_base@/@case_name@.meson',
            ],
            suite: 'fmt',
        )
    endif
endforeach
