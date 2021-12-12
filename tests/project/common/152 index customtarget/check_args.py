#!python3

import sys
import os
from pathlib import Path, PurePath

def main():
    if len(sys.argv) != 2:
        print("wrong argv")
        return 1

    rel = str(PurePath(sys.argv[1]).relative_to(os.getcwd()))

    if rel != 'gen.c':
        print(f"{rel} != gen.c")
        return 2
    Path('foo').touch()

    return 0

if __name__ == '__main__':
    sys.exit(main())
