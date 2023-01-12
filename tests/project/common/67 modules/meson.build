project('module test')

modtest = import('fs')
assert(modtest.found())

modtest = import('fs', required: get_option('disabled'))
assert(not modtest.found())

notfound = import('not-found', required: false)
assert(not notfound.found())

disabled = import('not-found', required: false, disabler: true)
assert(is_disabler(disabled))
