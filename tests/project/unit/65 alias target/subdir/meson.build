# TODO: run_target is not implemented yet
# r = run_target('run-target',
#   command: [python3, '-c', 'print("a run target was here")']
# )

# use this instead of run_target for now
r = executable('prog2', '../main.c',
  build_by_default : false)

ar = alias_target('aliased-run', r)
aat = alias_target('aliased-aliased-target', [ r, ar ])
