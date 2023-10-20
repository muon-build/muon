project('muon-python')

py_mod = import('python')
py = py_mod.find_installation()

if py.language_version().version_compare('< 3.6')
    error('Invalid Python version, only >= 3.6 is supported.')
endif
