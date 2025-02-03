# Mutability

In scripting mode mutability rules are relaxed slightly.  `list` and `dict`
types may be mutated by reference.

```meson
# Mutating a list with + and +=:
list = [0, 2, 3]
list[0] = 1
assert(list == [1, 2, 3])
list[0] += 1
assert(list == [2, 2, 3])
```

```meson
# Mutating a dict with + and +=:
dict = {'a': 1}
dict['a'] = 2
assert(dict == {'a': 2})
dict['a'] += 1
assert(dict == {'a': 3})
dict['b'] = 4
assert(dict == {'a': 3, 'b': 4})
```

Dictionaries may also be indexed using the `.` operator in scripting mode so the
above example could be rewritten as:

```meson
# some asserts removed
dict = {'a': 1}
dict.a = 2
dict.a += 1
dict.b = 4
assert(dict == {'a': 3, 'b': 4})
```

Re-assigning creates a copy[^copynote], mutations do not affect the copied variables.

```meson
list = [0, 1]
list2 = list
list[0] += 1

assert(list == [1, 1])
assert(list2 == [0, 1])
```

[^copynote]: Muon implements this using copy-on-write but the effect should be
    the same.
