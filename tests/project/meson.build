# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

if not host_machine.cpu_family().startswith('x86')
    fail_on_non_x86 = ['failing']
else
    fail_on_non_x86 = []
endif

tests = [
    # project tests created for muon
    ['muon/timeout', ['failing']],
    ['muon/sizeof_invalid'],
    ['muon/str'],
    ['muon/python', ['python']],
    ['muon/script_module'],
    ['muon/objc and cpp'],

    # project tests imported from meson unit tests

    # unit
    ['unit/65 alias target', ['python']],

    # project tests imported from meson
    ['meson-tests/common/1 trivial'],
    ['meson-tests/common/2 cpp'],
    ['meson-tests/common/3 static'],
    ['meson-tests/common/4 shared'],
    ['meson-tests/common/5 linkstatic'],
    ['meson-tests/common/6 linkshared'],
    ['meson-tests/common/7 mixed'],
    ['meson-tests/common/8 install', ['python']],
    ['meson-tests/common/9 header install'],
    ['meson-tests/common/10 man install'],
    ['meson-tests/common/11 subdir'],
    ['meson-tests/common/12 data'],
    # requires pch :)
    # ['meson-tests/common/13 pch', ['python', 'failing']],
    ['meson-tests/common/14 configure file', ['python']],
    ['meson-tests/common/15 if'],
    ['meson-tests/common/16 comparison'],
    ['meson-tests/common/17 array'],
    ['meson-tests/common/18 includedir'],
    ['meson-tests/common/19 header in file list'],
    ['meson-tests/common/20 global arg'],
    ['meson-tests/common/21 target arg'],
    ['meson-tests/common/22 object extraction', ['python']],
    ['meson-tests/common/23 endian'],
    ['meson-tests/common/24 library versions'],
    ['meson-tests/common/25 config subdir'],
    ['meson-tests/common/26 find program', ['python']],
    ['meson-tests/common/27 multiline string'],
    ['meson-tests/common/28 try compile'],
    ['meson-tests/common/29 compiler id'],
    ['meson-tests/common/30 sizeof'],
    ['meson-tests/common/31 define10'],
    ['meson-tests/common/32 has header'],
    ['meson-tests/common/33 run program', ['python']],
    ['meson-tests/common/34 logic ops'],
    ['meson-tests/common/35 string operations'],
    ['meson-tests/common/36 has function'],
    ['meson-tests/common/37 has member'],
    ['meson-tests/common/38 alignment'],
    ['meson-tests/common/39 library chain'],
    ['meson-tests/common/40 options'],
    ['meson-tests/common/41 test args', ['python']],
    ['meson-tests/common/42 subproject'],
    ['meson-tests/common/43 subproject options'],
    ['meson-tests/common/44 pkgconfig-gen', ['python']],
    ['meson-tests/common/45 custom install dirs'],
    ['meson-tests/common/46 subproject subproject'],
    ['meson-tests/common/47 same file name'],
    ['meson-tests/common/48 file grabber'],
    ['meson-tests/common/49 custom target', ['python', 'skip_analyze']],
    ['meson-tests/common/50 custom target chain', ['python']],
    ['meson-tests/common/51 run target', ['python']],
    ['meson-tests/common/52 object generator', ['python']],
    ['meson-tests/common/53 install script', ['python']],
    ['meson-tests/common/54 custom target source output', ['python']],
    ['meson-tests/common/55 exe static shared'],
    ['meson-tests/common/56 array methods'],
    ['meson-tests/common/57 custom header generator', ['python']],
    ['meson-tests/common/58 multiple generators', ['python']],
    ['meson-tests/common/59 install subdir'],
    ['meson-tests/common/60 foreach'],
    ['meson-tests/common/61 number arithmetic'],
    ['meson-tests/common/62 string arithmetic'],
    ['meson-tests/common/63 array arithmetic'],
    ['meson-tests/common/64 arithmetic bidmas'],
    ['meson-tests/common/65 build always', ['python']],
    ['meson-tests/common/66 vcstag', ['python']],
    ['meson-tests/common/67 modules'],
    ['meson-tests/common/68 should fail'],
    ['meson-tests/common/69 configure file in custom target', ['python']],
    ['meson-tests/common/70 external test program', ['python']],
    ['meson-tests/common/71 ctarget dependency', ['python']],
    ['meson-tests/common/72 shared subproject'],
    ['meson-tests/common/73 shared subproject 2'],
    ['meson-tests/common/74 file object'],
    ['meson-tests/common/75 custom subproject dir'],
    ['meson-tests/common/76 has type'],
    ['meson-tests/common/77 extract from nested subdir'],
    ['meson-tests/common/78 internal dependency'],
    ['meson-tests/common/79 same basename'],
    ['meson-tests/common/80 declare dep'],
    ['meson-tests/common/81 extract all'],
    ['meson-tests/common/82 add language'],
    [
        'meson-tests/common/83 identical target name in subproject',
        ['python'],
    ],
    ['meson-tests/common/84 plusassign'],
    ['meson-tests/common/85 skip subdir'],
    ['meson-tests/common/86 private include', ['python']],
    ['meson-tests/common/87 default options'],
    ['meson-tests/common/88 dep fallback', ['python']],
    ['meson-tests/common/89 default library'],
    ['meson-tests/common/90 gen extra', ['python']],
    ['meson-tests/common/91 benchmark'],
    ['meson-tests/common/92 test workdir', ['python']],
    ['meson-tests/common/93 suites'],
    ['meson-tests/common/94 threads'],
    ['meson-tests/common/95 manygen', ['python']],
    ['meson-tests/common/96 stringdef'],
    ['meson-tests/common/97 find program path', ['python']],
    ['meson-tests/common/98 subproject subdir'],
    ['meson-tests/common/99 postconf', ['python']],
    ['meson-tests/common/100 postconf with args', ['python']],
    ['meson-tests/common/101 testframework options'],
    ['meson-tests/common/102 extract same name'],
    ['meson-tests/common/103 has header symbol'],
    ['meson-tests/common/104 has arg'],
    ['meson-tests/common/105 generatorcustom', ['python']],
    ['meson-tests/common/106 multiple dir configure file', ['python']],
    ['meson-tests/common/107 spaces backslash'],
    ['meson-tests/common/108 ternary'],
    ['meson-tests/common/109 custom target capture', ['python']],
    ['meson-tests/common/110 allgenerate', ['python']],
    ['meson-tests/common/111 pathjoin'],
    ['meson-tests/common/112 subdir subproject'],
    ['meson-tests/common/113 interpreter copy mutable var on assignment'],
    ['meson-tests/common/114 skip'],
    ['meson-tests/common/115 subproject project arguments'],
    ['meson-tests/common/116 test skip'],
    ['meson-tests/common/117 shared module'],
    ['meson-tests/common/118 llvm ir and assembly'],
    ['meson-tests/common/119 cpp and asm'],
    ['meson-tests/common/120 extract all shared library'],
    ['meson-tests/common/121 object only target', ['python']],
    ['meson-tests/common/122 no buildincdir'],
    ['meson-tests/common/123 custom target directory install', ['python']],
    ['meson-tests/common/124 dependency file generation'],
    ['meson-tests/common/125 configure file in generator', ['python']],
    ['meson-tests/common/126 generated llvm ir', ['python']],
    ['meson-tests/common/127 generated assembly', ['python']],
    ['meson-tests/common/128 build by default targets in tests', ['python']],
    ['meson-tests/common/129 build by default', ['python']],
    ['meson-tests/common/130 include order', ['python']],
    ['meson-tests/common/131 override options'],
    ['meson-tests/common/132 get define'],
    ['meson-tests/common/133 c cpp and asm'],
    ['meson-tests/common/134 compute int'],
    ['meson-tests/common/135 custom target object output', ['python']],
    ['meson-tests/common/136 empty build file'],
    ['meson-tests/common/137 whole archive'],
    ['meson-tests/common/138 C and CPP link'],
    # requires meson instrospect
    # ['meson-tests/common/139 mesonintrospect from scripts', ['python', 'failing']],
    ['meson-tests/common/140 custom target multiple outputs', ['python']],
    ['meson-tests/common/141 special characters', ['python']],
    ['meson-tests/common/142 nested links'],
    ['meson-tests/common/143 list of file sources', ['python']],
    ['meson-tests/common/144 link depends custom target', ['python']],
    ['meson-tests/common/145 recursive linking'],
    ['meson-tests/common/146 library at root'],
    # relies on unstable-simd module
    # ['meson-tests/common/147 simd', ['failing']],
    ['meson-tests/common/148 shared module resolving symbol in executable'],
    ['meson-tests/common/149 dotinclude'],
    ['meson-tests/common/150 reserved targets', ['python']],
    ['meson-tests/common/151 duplicate source names'],
    ['meson-tests/common/152 index customtarget', ['python']],
    ['meson-tests/common/153 wrap file should not failed', ['git_clean']],
    ['meson-tests/common/154 includedir subproj'],
    ['meson-tests/common/155 subproject dir name collision'],
    # relies on config-tool
    # ['meson-tests/common/156 config tool variable', ['failing']],
    ['meson-tests/common/157 custom target subdir depend files', ['python']],
    ['meson-tests/common/158 disabler', ['skip_analyze']],
    ['meson-tests/common/159 array option'],
    [
        'meson-tests/common/160 custom target template substitution',
        ['python'],
    ],
    ['meson-tests/common/161 not-found dependency'],
    ['meson-tests/common/162 subdir if_found'],
    ['meson-tests/common/163 default options prefix dependent defaults'],
    ['meson-tests/common/164 dependency factory'],
    ['meson-tests/common/165 get project license'],
    ['meson-tests/common/166 yield'],
    ['meson-tests/common/167 subproject nested subproject dirs'],
    ['meson-tests/common/168 preserve gendir', ['python']],
    ['meson-tests/common/169 source in dep', ['python']],
    ['meson-tests/common/170 generator link whole', ['python']],
    ['meson-tests/common/171 initial c_args'],
    [
        'meson-tests/common/172 identical target name in subproject flat layout',
    ],
    ['meson-tests/common/173 as-needed'],
    ['meson-tests/common/174 ndebug if-release enabled'],
    ['meson-tests/common/175 ndebug if-release disabled'],
    ['meson-tests/common/176 subproject version'],
    ['meson-tests/common/177 subdir_done'],
    ['meson-tests/common/178 bothlibraries', ['python']],
    ['meson-tests/common/179 escape and unicode', ['python']],
    ['meson-tests/common/180 has link arg'],
    ['meson-tests/common/181 same target name flat layout'],
    ['meson-tests/common/182 find override', ['python']],
    ['meson-tests/common/183 partial dependency'],
    # requires dependency 'meson-tests/openmp'
    # ['meson-tests/common/184 openmp', ['failing']],
    ['meson-tests/common/185 same target name'],
    ['meson-tests/common/186 test depends', ['python']],
    ['meson-tests/common/187 args flattening'],
    ['meson-tests/common/188 dict'],
    ['meson-tests/common/189 check header'],
    ['meson-tests/common/190 install_mode'],
    ['meson-tests/common/191 subproject array version'],
    ['meson-tests/common/192 feature option'],
    ['meson-tests/common/193 feature option disabled'],
    ['meson-tests/common/194 static threads'],
    ['meson-tests/common/195 generator in subdir', ['python']],
    ['meson-tests/common/196 subproject with features'],
    ['meson-tests/common/197 function attributes'],
    ['meson-tests/common/198 broken subproject'],
    ['meson-tests/common/199 argument syntax'],
    ['meson-tests/common/200 install name_prefix name_suffix'],
    ['meson-tests/common/201 kwarg entry'],
    ['meson-tests/common/202 custom target build by default', ['python']],
    ['meson-tests/common/203 find_library and headers'],
    ['meson-tests/common/204 line continuation'],
    ['meson-tests/common/205 native file path override'],
    ['meson-tests/common/206 tap tests'],
    ['meson-tests/common/207 warning level 0'],
    ['meson-tests/common/208 link custom', ['python']],
    [
        'meson-tests/common/209 link custom_i single from multiple',
        ['python'],
    ],
    [
        'meson-tests/common/210 link custom_i multiple from multiple',
        ['python'],
    ],
    ['meson-tests/common/211 dependency get_variable method'],
    ['meson-tests/common/212 source set configuration_data'],
    ['meson-tests/common/213 source set dictionary'],
    ['meson-tests/common/214 source set custom target', ['python']],
    ['meson-tests/common/215 source set realistic example'],
    [
        'meson-tests/common/216 custom target input extracted objects',
        ['python'],
    ],
    ['meson-tests/common/217 test priorities', ['python']],
    ['meson-tests/common/218 include_dir dot'],
    ['meson-tests/common/219 include_type dependency'],
    ['meson-tests/common/220 fs module'],
    ['meson-tests/common/221 zlib'],
    # requires machine files
    # ['meson-tests/common/222 native prop', ['failing']],
    ['meson-tests/common/223 persubproject options'],
    ['meson-tests/common/224 arithmetic operators'],
    ['meson-tests/common/225 link language'],
    [
        'meson-tests/common/226 link depends indexed custom target',
        ['python'],
    ],
    # skip this test for now because it takes too long
    # ['meson-tests/common/227 very long command line', ['python']],
    ['meson-tests/common/228 custom_target source', ['python']],
    ['meson-tests/common/229 disabler array addition'],
    # requires unstable_external_project module
    # ['meson-tests/common/230 external project', ['failing']],
    ['meson-tests/common/231 subdir files'],
    ['meson-tests/common/232 dependency allow_fallback'],
    ['meson-tests/common/233 wrap case'],
    ['meson-tests/common/234 get_file_contents'],
    ['meson-tests/common/235 invalid standard overridden to valid'],
    [
        'meson-tests/common/236 proper args splitting',
        [],
        {'env': {'CFLAGS': '-DFOO -DBAR'}},
    ],
    ['meson-tests/common/237 fstrings'],
    ['meson-tests/common/238 dependency include_type inconsistency'],
    ['meson-tests/common/239 includedir violation'],
    ['meson-tests/common/240 dependency native host == build'],
    ['meson-tests/common/241 set and get variable'],
    ['meson-tests/common/242 custom target feed', ['python']],
    ['meson-tests/common/243 escape++'],
    ['meson-tests/common/244 variable scope'],
    ['meson-tests/common/245 custom target index source', ['python']],
    ['meson-tests/common/246 dependency fallbacks'],
    ['meson-tests/common/247 deprecated option', ['python']],
    ['meson-tests/common/248 install_emptydir'],
    ['meson-tests/common/249 install_symlink'],
    ['meson-tests/common/250 system include dir'],
    ['meson-tests/common/251 add_project_dependencies'],
    ['meson-tests/common/252 install data structured', ['python']],
    ['meson-tests/common/253 subproject dependency variables'],
    ['meson-tests/common/254 long output'],
    # not relevant to muon
    # ['meson-tests/common/255 module warnings'],
    ['meson-tests/common/256 subproject extracted objects'],
    ['meson-tests/common/257 generated header dep'],
    ['meson-tests/common/258 subsubproject inplace'],
    ['meson-tests/common/259 preprocess'],
    ['meson-tests/common/260 declare_dependency objects'],
    # ['meson-tests/common/261 testcase clause'],
    ['meson-tests/common/262 generator chain'],
    ['meson-tests/common/263 internal dependency includes in checks'],
    ['meson-tests/common/264 required keyword in has functions'],
    ['meson-tests/common/265 default_options dict'],
    ['meson-tests/common/266 format string'],
    ['meson-tests/common/267 default_options in find_program'],
    # This test depends on symlinks being created prior to the test being run.
    # ['meson-tests/common/268 install functions and follow symlinks'],
    ['meson-tests/common/269 configure file output format'],
    ['meson-tests/common/270 int_to_str_fill'],
    ['meson-tests/common/271 env in generator.process'],
    # requires unity
    # ['meson-tests/common/272 unity'],
    ['meson-tests/common/273 customtarget exe for test'],
    ['meson-tests/common/274 environment'],
    ['meson-tests/common/275 required keyword in compiles functions'],
    ['meson-tests/common/276 generator custom_tgt subdir'],
    ['meson-tests/common/277 custom target private dir'],

    # python
    ['meson-tests/python/1 basic', ['python']],
    # The following need the currently unimplemented `dependency()`
    # ['meson-tests/python/2 extmodule', ['python']],
    # ['meson-tests/python/3 cython', ['python']],
    # ['meson-tests/python/4 custom target depends extmodule', ['python']],
    # ['meson-tests/python/8 different python versions', ['python']],
    ['meson-tests/python/5 modules kwarg', ['python']],
    ['meson-tests/python/6 failing subproject', ['python', 'failing']],
    ['meson-tests/python/7 install path', ['python']],
    # These need the currently unimplemented `extension_module()`
    # ['meson-tests/python/9 extmodule limited api', ['python']],
    # ['meson-tests/python/10 extmodule limited api disabled', ['python']],

    # native
    ['meson-tests/native/1 trivial'],
    ['meson-tests/native/2 global arg'],
    ['meson-tests/native/3 pipeline', ['python']],
    ['meson-tests/native/4 tryrun'],
    ['meson-tests/native/5 install script', ['python']],
    ['meson-tests/native/6 add language'],
    ['meson-tests/native/7 selfbuilt custom'],
    ['meson-tests/native/8 external program shebang parsing'],
    ['meson-tests/native/9 override with exe'],

    # nasm
    ['meson-tests/nasm/1 configure file'],
    ['meson-tests/nasm/2 asm language', fail_on_non_x86],
    ['meson-tests/nasm/3 nasm only'],
    ['meson-tests/nasm/4 through configure'],

    # keyval
    ['meson-tests/keyval/1 basic'],
    ['meson-tests/keyval/2 subdir'],
    ['meson-tests/keyval/3 load_config files'],
    ['meson-tests/keyval/4 load_config builddir'],

    # frameworks
    ['meson-tests/frameworks/6 gettext'],
    ['meson-tests/frameworks/7 gnome'],

    # objc
    ['meson-tests/objc/1 simple', ['objc']],
    ['meson-tests/objc/2 nsstring', ['objc']],
    ['meson-tests/objc/3 objc args', ['objc']],
    ['meson-tests/objc/4 c++ project objc subproject', ['objc']],
    ['meson-tests/objc/5 objfw', ['objc']],

    # objcpp
    ['meson-tests/objcpp/1 simple', ['objc']],
    ['meson-tests/objcpp/2 objc++ args', ['objc']],
    ['meson-tests/objcpp/3 objfw', ['objc']],
]

if dep_dict['samurai']
    ninja = '@0@ samu'.format(muon.full_path())
else
    ninja = find_program('ninja', 'samu', required: false)
    if ninja.found()
        ninja = ninja.full_path()
    else
        warning('ninja/samu not found, skipping project tests')
        subdir_done()
    endif
endif

if git.found()
    meson_tests_dir = meson.current_source_dir() / 'meson-tests'
    meson_tests_sha = '38633de9c04ac7a57b09c8a7a83ab88a12409464'

    if fs.is_dir('meson-tests')
        meson_tests_clean = run_command(git, '-C', meson_tests_dir, 'diff', '--quiet', check: false).returncode() == 0

        # If the meson-tests repo has no modifications and is not up to date,
        # force update it.
        if meson_tests_clean and run_command(
            git,
            '-C', meson_tests_dir,
            'merge-base',
            '--is-ancestor', meson_tests_sha,
            'HEAD',
            check: false,
        ).returncode() != 0
            warning('force-updating meson-tests repo')
            run_command(
                git,
                '-C', meson_tests_dir,
                'fetch',
                '--all',
                capture: false,
                check: true,
            )

            run_command(
                git,
                '-C', meson_tests_dir,
                'reset',
                '--hard', 'origin/master',
                capture: false,
                check: true,
            )
        endif
    else
        run_command(
            git,
            'clone',
            get_option('meson_tests_repo'),
            meson_tests_dir,
            capture: false,
            check: true,
        )
    endif
endif

skip_meson_tests = not fs.is_dir('meson-tests')
if skip_meson_tests
    warning('meson-tests not found, tests imported from meson will not be run')
endif

test_dir = meson.current_build_dir()

is_git_repo = fs.is_dir('meson-tests' / '.git')

foreach t : tests
    # The exit code to use when a test is skipped
    test = t[0]
    extra = t.get(1, [])
    kwargs = t.get(2, {})

    if test.startswith('meson-test') and skip_meson_tests
        continue
    endif

    # skip objc tests on non darwin platforms
    # TODO: add objc / objcpp compiler detection and skip based on that instead
    if 'objc' in extra and host_machine.system() != 'darwin'
        continue
    endif

    skip_exit_code = 77
    if 'failing' in extra
        skip_exit_code = 1
        kwargs += {'should_fail': true}
    endif

    skip_analyze = 'skip_analyze' in extra ? 1 : 0
    git_clean = (is_git_repo and 'git_clean' in extra) ? 1 : 0

    suites = ['project', test.split('/')[0]]

    if 'python' in extra
        if not python3.found()
            continue
        endif

        suites += 'requires_python'
    endif

    test(
        test,
        muon,
        args: [
            'internal',
            'eval',
            meson.current_source_dir() / 'runner.meson',
            muon,
            ninja,
            meson.current_source_dir() / test,
            test_dir / test,
            skip_exit_code.to_string(),
            skip_analyze.to_string(),
            git_clean.to_string(),
        ],
        suite: suites,
        kwargs: kwargs,
    )
endforeach
