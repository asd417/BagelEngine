#!/usr/bin/env python3
"""Derive src/'s abstraction layers from its #include graph.

Prints, for every file under src/:
  - header cycles (should always be none)
  - depth = longest internal include chain to a leaf (0 = no internal includes)
  - in-degree = how many src/ files include this header (high = low level)

Depth measures HEADER weight, not conceptual level: a thin header on a heavy .cpp
(e.g. pose_gizmo.hpp) scores low. Use it as a signal, not a verdict.

Only sees #include edges. Runtime coupling through globals -- BGLDevice::device(),
CONSOLE, BGLJolt::GetInstance() -- is invisible here and is real.

Usage:  python tools/include_layers.py            # from repo root
        python tools/include_layers.py --csv      # machine-readable
"""
import os
import re
import sys
from collections import defaultdict

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
SRC = os.path.normpath(SRC)
EXTS = (".cpp", ".hpp", ".h")
SKIP_DIRS = {".tmp"}

# Comments must be stripped first: bagel_descriptors.hpp carries //#include lines that
# otherwise fabricate edges (and four phantom cycles).
inc_re = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
block_comment = re.compile(r"/\*.*?\*/", re.S)
line_comment = re.compile(r"//.*?$", re.M)


def strip_comments(text):
    return line_comment.sub("", block_comment.sub("", text))


def collect_files():
    out = []
    for root, dirs, names in os.walk(SRC):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in names:
            if n.endswith(EXTS):
                rel = os.path.relpath(os.path.join(root, n), SRC)
                out.append(rel.replace("\\", "/"))
    return sorted(out)


def build_graph(files):
    by_base = defaultdict(list)
    for f in files:
        by_base[os.path.basename(f)].append(f)
    fileset = set(files)

    def resolve(inc, frm):
        cand = os.path.normpath(os.path.join(os.path.dirname(frm), inc)).replace("\\", "/")
        if cand in fileset:
            return cand
        if inc in fileset:
            return inc
        hits = by_base.get(os.path.basename(inc), [])
        return hits[0] if len(hits) == 1 else None  # ambiguous basename -> external

    edges = {}
    for f in files:
        with open(os.path.join(SRC, f), encoding="utf-8", errors="replace") as fh:
            text = strip_comments(fh.read())
        deps = {resolve(i, f) for i in inc_re.findall(text)}
        edges[f] = {d for d in deps if d and d != f}
    return edges


def find_cycles(headers, hedges):
    cycles, stack, color = [], [], {h: 0 for h in headers}  # 0 white 1 grey 2 black

    def dfs(u):
        color[u] = 1
        stack.append(u)
        for v in sorted(hedges.get(u, ())):
            if color[v] == 1:
                cycles.append(stack[stack.index(v):] + [v])
            elif color[v] == 0:
                dfs(v)
        stack.pop()
        color[u] = 2

    sys.setrecursionlimit(10000)
    for h in sorted(headers):
        if color[h] == 0:
            dfs(h)
    return cycles


def main():
    files = collect_files()
    edges = build_graph(files)
    headers = {f for f in files if f.endswith((".hpp", ".h"))}
    hedges = {h: {d for d in edges[h] if d in headers} for h in headers}

    cycles = find_cycles(headers, hedges)

    depth = {}

    def d(u, seen=()):
        if u in depth:
            return depth[u]
        if u in seen:
            return 0  # cycle guard
        kids = [v for v in edges[u] if v in headers]
        depth[u] = 1 + max([d(v, seen + (u,)) for v in kids]) if kids else 0
        return depth[u]

    for f in files:
        d(f)

    indeg = defaultdict(int)
    for f in files:
        for dep in edges[f]:
            indeg[dep] += 1

    if "--csv" in sys.argv:
        print("file,depth,in_degree")
        for f in files:
            print(f"{f},{depth[f]},{indeg[f]}")
        return 0

    print("=== HEADER CYCLES ===")
    seen = set()
    for c in cycles:
        key = frozenset(c)
        if key not in seen:
            seen.add(key)
            print("  " + " -> ".join(c))
    if not cycles:
        print("  none")

    print("\n=== HEADERS BY IN-DEGREE (high = depended on = low level) ===")
    for f in sorted(headers, key=lambda x: (-indeg[x], x))[:20]:
        print(f"  {indeg[f]:3d}  {f}")

    print("\n=== ALL FILES BY DEPTH (0 = leaf) ===")
    for f in sorted(files, key=lambda x: (depth[x], x)):
        print(f"  {depth[f]:2d}  {f}")

    return 1 if cycles else 0


if __name__ == "__main__":
    sys.exit(main())
