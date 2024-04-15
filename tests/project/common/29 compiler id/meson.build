project('compiler_id')

# different than meson: only c and cpp supported
foreach lang : ['c', 'cpp']
    #, 'fortran', 'objc', 'objcpp']

    if not add_languages(lang, required: false)
        continue
    endif

    comp = meson.get_compiler(lang)

    message(lang + ' compiler name is: ' + comp.get_id())

    message(lang + ' linker name is: ' + comp.get_linker_id())

endforeach
