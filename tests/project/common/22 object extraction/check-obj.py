#! /usr/bin/env python3

from pathlib import PurePath
import json
import sys
import os

cc = None
output = None

# Only the ninja backend produces compile_commands.json
if sys.argv[1] == 'ninja':
    with open('compile_commands.json', 'r') as f:
        cc = json.load(f)
    output = set((x['output'] for x in cc))

for obj in sys.argv[2:]:
    obj = str(PurePath(obj).relative_to(os.getcwd()))
    if not os.path.exists(obj):
        sys.exit(f'File {obj} not found.')
    if sys.argv[1] == 'ninja' and obj not in output:
        sys.exit(1)
    print('Verified', obj)
