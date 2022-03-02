#! /usr/bin/env python3

import json
import sys
import os

cc = None
output = None

# Only the ninja backend produces compile_commands.json
if sys.argv[1] == "ninja":
    if os.path.exists("compile_commands.json"):
        with open("compile_commands.json", "r") as f:
            cc = json.load(f)
        output = set((x["output"] for x in cc))

for obj in sys.argv[2:]:
    if not os.path.exists(obj):
        sys.exit(f"File {obj} not found.")
    if output and obj not in output:
        sys.exit(f"File {obj} not in output.")

    print("Verified", obj)
