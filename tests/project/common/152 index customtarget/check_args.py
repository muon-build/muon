#!python3

import sys
import os
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        print("wrong argv")
        return 1

    if sys.argv[1] != "gen.c":
        print(f"{sys.argv[1]} != gen.c")
        return 2
    Path("foo").touch()

    return 0


if __name__ == "__main__":
    sys.exit(main())
