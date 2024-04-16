# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

project('muon-python')

py_mod = import('python')
py = py_mod.find_installation()

if py.language_version().version_compare('< 3.6')
    error('Invalid Python version, only >= 3.6 is supported.')
endif

py_nonexisting = import('nonexisting_python_b075e5a', required: false)

if py_nonexisting.found()
    error('Python installation found that is unlikely to exist.')
endif

# kwargs
pymod_with_modules = py_mod.find_installation(
    'python3',
    modules: ['sys', 'os', 'sysconfig', 'json'],
    required: false,
)

if not pymod_with_modules.found()
    error('Python installation with standard modules not found.')
endif

pymod_nonexisting_modules = py_mod.find_installation(
    'python3',
    modules: ['sys', 'os', 'nonexisting_module_ae7a9b0'],
    required: false,
)

if pymod_nonexisting_modules.found()
    error('Python installation found with a module unlikely to exist.')
endif

if py.has_path('stdlib')
    stdlib_path = py.get_path('stdlib', 'not-found-default')
    if stdlib_path == 'not-found-default'
        error('error retrieving an existing path.')
    endif
    message(stdlib_path)
endif

if not py.has_path('nonexisting_path_34ac2e')
    nonexisting_path = py.get_path(
        'nonexisting_path_34ac2e',
        'not-found-default',
    )
    if nonexisting_path != 'not-found-default'
        error('found a non-existing path.')
    endif
    message(nonexisting_path)
endif

if py.has_variable('py_version')
    pyversion = py.get_variable('py_version', 'not-found-default')
    if pyversion == 'not-found-default'
        error('error retrieving an existing variable.')
    endif
    message(pyversion)
endif

if not py.has_variable('nonexisting_version_1cf0ed0')
    nonexisting_version = py.get_variable(
        'nonexisting_version_1cf0ed0',
        'not-found-default',
    )
    if nonexisting_version != 'not-found-default'
        error('found a non-existing variable')
    endif
    message(nonexisting_version)
endif
