#!/usr/bin/env python3
"""The layering rule of the signal library.

signals/ is made of layers, listed here from the bottom up. A file of a layer
may include files of its own layer and of the layers before it, never of a
layer after it : the directories form a DAG, and this script is the proof,
recomputed from the #include lines. Exit status 1 on the first violation.
"""
import os, re, sys
LAYERS = ["terms", "types", "traverse", "analysis", "normalize", "forms", "passes", "session"]
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "signals")
rank, layer_of = {}, {}
for i, l in enumerate(LAYERS):
    d = os.path.join(root, l)
    if not os.path.isdir(d):
        print(f"missing layer directory : {d}"); sys.exit(1)
    for f in os.listdir(d):
        base = f.rsplit(".", 1)[0]
        layer_of[base] = l; rank[base] = i
unknown = [f for f in os.listdir(root) if not os.path.isdir(os.path.join(root, f))]
if unknown:
    print("files outside every layer :", " ".join(sorted(unknown))); sys.exit(1)
bad = []
for l in LAYERS:
    d = os.path.join(root, l)
    for f in sorted(os.listdir(d)):
        base = f.rsplit(".", 1)[0]
        for h in re.findall(r'#include "([^"]+)\.hh"', open(os.path.join(d, f)).read()):
            h = os.path.basename(h)
            if h in rank and rank[h] > rank[base]:
                bad.append(f"{l}/{f} includes {layer_of[h]}/{h}.hh")
if bad:
    print("layering violations (a layer includes a layer above it) :")
    for b in bad: print("  ", b)
    sys.exit(1)
print(f"layers OK : {len(rank)} files in {len(LAYERS)} layers, no upward include")
