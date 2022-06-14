project('header install')

as_array = ['subdir.h']

subdir('vanishing_subdir')
subdir('sub')

install_headers('rootdir.h')
install_headers(as_array, subdir: 'subdir')
install_headers(subheader)
install_headers(disabler())
