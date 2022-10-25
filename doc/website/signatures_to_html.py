# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

import sys
import itertools


def parse(file):
    sigs = {}

    with open(file, "r") as f:
        argi = {
            "posargs:": 0,
            "varargs:": 1,
            "optargs:": 2,
            "kwargs:": 3,
            "returns:": 4,
        }

        fname = None
        args = [[], [], [], [], []]
        curargs = None
        extension = False

        for line in f.read().strip().split("\n"):
            if line[0] != " ":
                if fname:
                    sigs[fname] = Sig(fname, *args, extension)
                    fname = None
                    args = [[], [], [], [], []]
                    curargs = None
                    extension = False

                l = line.split(":")
                if len(l) > 1 and l[0] == "extension":
                    extension = True
                fname = l[-1]
            elif line[2] != " ":
                curargs = argi[line.strip()]
            else:
                v = line.strip()
                if curargs == 3:  # kwargs
                    v = v.split(": ")

                args[curargs].append(v)

        if fname:
            sigs[fname] = Sig(fname, *args, extension)

    return sigs


class Sig:
    def empty(name):
        return Sig(name, [], [], [], [], [], False)

    def __init__(self, name, posargs, varargs, optargs, kwargs, returns, extension):
        self.name = name
        self.posargs = posargs
        self.varargs = varargs
        self.optargs = optargs
        self.kwargs = {self.esc(k): v for k, v in self.normalize_kw(kwargs)}
        if returns:
            self.returns = returns[0]
        else:
            self.returns = returns
        self.extension = extension

    def normalize_kw(self, kwargs):
        applies_to = [
            "executable",
            "library",
            "static_library",
            "shared_library",
            "shared_module",
            "build_target",
            "both_libraries",
        ]

        if self.name not in applies_to:
            return kwargs

        prefixes = ["c_", "cpp_", "objc_"]
        delete = []
        add = {}
        for kw, v in kwargs:
            for pre in prefixes:
                if kw.startswith(pre):
                    delete.append(kw)
                    add["<lang>_" + kw[len(pre) :]] = v
                    break

        add = [[k, v] for k, v in add.items()]

        return [x for x in kwargs if x[0] not in delete] + add

    def esc(self, c):
        return str(c).replace("<", "&#60;").replace(">", "&#62;")

    def arg_count(self):
        return (
            len(self.posargs) + len(self.varargs) + len(self.optargs) + len(self.kwargs)
        )


muon = parse(sys.argv[1])
meson = parse(sys.argv[2])


class Table:
    def __init__(self, thead):
        self.thead = thead
        self.rows = []

    def __add__(self, other):
        self.rows.append(other)
        return self

    def html(self):
        return (
            "<table><thead>"
            + self.thead.html()
            + "</thead><tbody>"
            + "".join([r.html() for r in self.rows])
            + "</tbody></table>"
        )


class Row:
    def __init__(self, *cols):
        self.cols = cols

    def html(self):
        return (
            "<tr>" + "".join(["<td>" + str(c) + "</td>" for c in self.cols]) + "</tr>"
        )


class FuncInfo:
    def __init__(self, name):
        self.name = name
        self.me = None
        self.mu = None


def _parse_type(t, in_container=False):
    parsed = []
    name = ""
    i = 0
    while i < len(t):
        c = t[i]
        if c == "[":
            (n, sub) = _parse_type(t[i + 1 :], in_container=True)
            parsed.append(
                (
                    name,
                    sub,
                )
            )
            name = ""
            i += n + 1
            continue
        elif c == "]":
            if name:
                parsed.append(name)
            return (i + 1, parsed)
        elif c == " ":
            i += 1
            continue
        elif c == "|":
            if name:
                parsed.append(name)
            name = ""
        else:
            name += c

        i += 1

    if name:
        parsed.append(name)
    return (i, parsed)


def parse_type(t):
    if t is None:
        return []

    (_, parsed) = _parse_type(t)
    return parsed


# Attempt to detect if a signature indicates listification and remove the
# duplicate part
def normalize_listify(tp):
    if not tp:
        return tp

    nl = []
    l = set()

    for t in tp:
        if type(t) is tuple:
            if t[0] == "list":
                l = t[1]
            else:
                return tp
        else:
            nl.append(t)

    if set(nl) == set(l):
        return [("list", nl)]
    else:
        return tp


def assemble_type(t):
    if type(t) is list:

        def sort_func(v):
            assert type(v) is not list

            if type(v) is tuple:
                return v[0]
            else:
                return v

        t.sort(key=sort_func)
        return " | ".join(assemble_type(x) for x in t)
    elif type(t) is tuple:
        return t[0] + "[" + assemble_type(t[1]) + "]"
    else:
        return t


def neutral(text):
    return f'<span class="neutral">{text}</span>'


def positive(text):
    return f'<span class="positive">{text}</span>'


def negative(text):
    return f'<span class="negative">{text}</span>'


def tdiff(a, b):
    s = []
    for t in a:
        if type(t) is tuple:
            other = None
            for u in b:
                if type(u) is tuple and t[0] == u[0]:
                    other = u[1]
                    break

            if other:
                s.append(neutral(f"{t[0]}[{tdiff(t[1], other)}]"))
            else:
                s.append(positive(f"{t[0]}[{assemble_type(t[1])}]"))
        else:
            if t in b:
                s.append(neutral(t))
            else:
                s.append(positive(t))

    return " | ".join(s)


def normalize_types(tp):
    conts = []
    ntp = set()
    for t in tp:
        if type(t) is tuple:
            sub = normalize_types(t[1])
            if not sub:
                ntp |= set([t[0]])
            else:
                conts.append((t[0], sub))
        else:
            ntp |= set(
                {
                    "custom_idx": ["file"],
                    "extracted_obj": ["file"],
                    "void": [],
                    "tgt": ["build_tgt", "custom_tgt", "both_libs"],
                    "lib": ["build_tgt", "both_libs"],
                    "exe": ["build_tgt"],
                }.get(t, [t])
            )

    def sort_func(v):
        assert type(v) is not list

        if type(v) is tuple:
            return v[0]
        else:
            return v

    return sorted(list(ntp) + conts, key=sort_func)


def typecomp(mu, me):
    a = normalize_listify(normalize_types(parse_type(mu)))
    b = normalize_listify(normalize_types(parse_type(me)))
    return (tdiff(a, b), tdiff(b, a))


func_tbl = Table(Row("function", "status", "muon return", "meson return"))
arg_tbls = []

all_funcs = set(muon.keys()) | set(meson.keys())
methods = set([f for f in all_funcs if "." in f])
kernel = all_funcs - methods

for f in sorted(kernel) + sorted(methods):
    if f in ["custom_idx.full_path", "custom_tgt.[index]"]:
        continue

    r = FuncInfo(f)
    if f in muon:
        flink = f'<a href="#{f}">{f}</a>'
        r.mu = muon[f]
    else:
        flink = f

    if f in meson:
        r.me = meson[f]

    support = positive("supported")

    if f not in muon:
        r.mu = Sig.empty(f)
        support = negative("unsupported")
    elif f not in meson:
        r.me = Sig.empty(f)
        if muon[f].extension:
            support = "muon extension"
        else:
            support = positive("supported*")

    func_tbl += Row(flink, support, *typecomp(r.mu.returns, r.me.returns))

    t = Table(Row("kind", "keyword", "muon type", "meson type"))

    for k in ["posarg", "vararg", "optarg"]:
        for a, b in itertools.zip_longest(
            getattr(r.mu, k + "s"), getattr(r.me, k + "s")
        ):
            t += Row(k, "", *typecomp(a, b))

    for k in sorted(set(r.me.kwargs.keys()) | set(r.mu.kwargs.keys())):
        t1 = None
        t2 = None

        if k in r.mu.kwargs:
            t1 = r.mu.kwargs[k]

        if k in r.me.kwargs:
            t2 = r.me.kwargs[k]

        t += Row("kwarg", k, *typecomp(t1, t2))

    if f in muon:
        arg_tbls.append((r, t))

module_tbl = Table(Row("module", "status"))
for m in ["fs", "keyval", "pkgconfig", "sourceset"]:
    module_tbl += Row(m, positive("supported"))

for m in ["python3", "python"]:
    module_tbl += Row(m, "partial")

for m in [
    "cmake",
    "dlang",
    "gnome",
    "hotdoc",
    "i18n",
    "java",
    "modtest",
    "qt",
    "qt4",
    "qt5",
    "qt6",
    "unstable-cuda",
    "unstable-external_project",
    "unstable-icestorm",
    "unstable-rust",
    "unstable-simd",
    "unstable-wayland",
    "windows",
]:
    module_tbl += Row(m, negative("unsupported"))

print(
    """<!DOCTYPE html>
<html><head>
<link rel="stylesheet" href="status.css" />
<title>muon implementation status</title>
<link rel="icon" type="image/svg+xml" href="muon_logo.svg">
</head><body><div class="wrapper">
"""
)

print('<div class="item">')
print(f"<h1>Modules</h1>")
print(module_tbl.html())
print(f"<h1>Functions and methods</h1>")
print(func_tbl.html())
print("</div>")
for r, at in arg_tbls:
    print("<hr>")
    print('<div class="item">')
    print(f'<h1 id="{r.name}">{r.name}</h1>')
    if at.rows:
        print(at.html())
    else:
        print("no arguments")
    print("</div>")
print("</div></body>")
