# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

#!/usr/bin/env python3

import glob
import json
import os
import shlex
import stat
import sys


def die(msg):
    print(msg)
    sys.exit(1)


def write_check_script(dat, path):
    s = "#!/bin/sh\nset -eux\n"

    for e in dat:
        if e["type"] == "dir":
            test = "-d"
        elif e["type"] == "file":
            test = "-f"
        else:
            # TODO: handle other file types
            continue

        file = shlex.quote(e["file"])
        s += f'test {test} "$DESTDIR"/{file}\n'

    with open(path, "w") as f:
        f.write(s)
    os.chmod(path, 0o755)


def main(argc, argv):
    if argc < 2:
        die(f"usage: {argv[0]} <path/to/tests>")

    for d in glob.glob(f"{argv[1]}/*"):
        test_json = d + "/test.json"
        check_script = d + "/check.sh"

        if not os.access(test_json, os.F_OK):
            continue

        with open(test_json) as f:
            dat = json.load(f)

        if "installed" in dat:
            write_check_script(dat["installed"], check_script)


if __name__ == "__main__":
    main(len(sys.argv), sys.argv)
