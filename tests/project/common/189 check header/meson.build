project('check header', 'c', 'cpp')

host_system = host_machine.system()

non_existent_header = 'ouagadougou.h'

# Copy it into the builddir to ensure that it isn't found even if it's there
configure_file(
    input: non_existent_header,
    output: non_existent_header,
    copy: true,
)

fallback = ''

foreach comp : [meson.get_compiler('c'), meson.get_compiler('cpp')]
    assert(comp.check_header('stdio.h', prefix: fallback), 'Stdio missing.')

    # stdio.h doesn't actually need stdlib.h, but just test that setting the
    # prefix does not result in an error.
    assert(
        comp.check_header('stdio.h', prefix: '#include <stdlib.h>' + fallback),
        'Stdio missing.',
    )

    # Test that check_header behaves differently than has_header. The second
    # check without windows.h will fail with check_header.
    # We only do this check on MSVC because MinGW often defines its own wrappers
    # that pre-include windows.h
    if comp.get_id() == 'msvc'
        assert(
            comp.check_header(
                'XInput.h',
                prefix: '#include <windows.h>' + fallback,
            ),
            'XInput.h should not be missing on Windows',
        )
        assert(not comp.check_header('XInput.h'), 'XInput.h needs windows.h')
    endif

    # This header exists in the source and the builddir, but we still must not
    # find it since we are looking in the system directories.
    assert(
        not comp.check_header(non_existent_header, prefix: fallback),
        'Found non-existent header.',
    )
endforeach
