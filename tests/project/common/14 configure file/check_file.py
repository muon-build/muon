#!/usr/bin/env python3

import os
import sys


def permit_osx_workaround(m1, m2):
    import platform

    if platform.system().lower() != "darwin":
        return False
    if m2 % 10000 != 0:
        return False
    if m1 // 10000 != m2 // 10000:
        return False
    return True


if len(sys.argv) == 2:
    assert os.path.exists(sys.argv[1])
elif len(sys.argv) == 3:
    f1 = sys.argv[1]
    f2 = sys.argv[2]

    import filecmp

    if not filecmp.cmp(f1, f2):
        raise RuntimeError(f"{f1!r} != {f2!r}")
else:
    raise AssertionError
