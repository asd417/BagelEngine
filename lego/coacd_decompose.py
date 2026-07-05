#!/usr/bin/env python3
"""CoACD approximate convex decomposition helper for the BagelEngine collision baker.

Invoked out-of-process by the (g++) baker so CoACD never has to be linked:

    python coacd_decompose.py <in.obj> <out.obj> <threshold> <max_hulls> [resolution] [mcts_iter] [decimate 0|1] [max_ch_vertex] [real_metric 0|1]

Reads a triangle-soup OBJ (v / f lines), runs CoACD, and writes the convex pieces to
one OBJ with one `o hullN` group per piece (vertices emitted directly under each group
so the baker can collect them by group and re-hull). Exit code 0 on success.

Requires the `coacd` and `numpy` packages -- see setup_coacd.bat / setup_coacd.sh.
"""
import sys


def read_obj(path):
    verts, faces = [], []
    with open(path, "r") as f:
        for line in f:
            if line.startswith("v "):
                _, x, y, z = line.split()[:4]
                verts.append((float(x), float(y), float(z)))
            elif line.startswith("f "):
                # Face indices may be "i", "i/j", "i/j/k"; take the position index only.
                idx = [int(tok.split("/")[0]) for tok in line.split()[1:]]
                # OBJ is 1-based; triangulate a fan for polygons (CoACD input is triangles).
                for k in range(1, len(idx) - 1):
                    faces.append((idx[0] - 1, idx[k] - 1, idx[k + 1] - 1))
    return verts, faces


def write_groups(path, parts):
    with open(path, "w") as f:
        f.write("# CoACD convex pieces (LDU)\n")
        base = 0
        for i, (verts, faces) in enumerate(parts):
            f.write("o hull{}\n".format(i))
            for v in verts:
                f.write("v {} {} {}\n".format(v[0], v[1], v[2]))
            for tri in faces:
                f.write("f {} {} {}\n".format(tri[0] + 1 + base, tri[1] + 1 + base, tri[2] + 1 + base))
            base += len(verts)


def main():
    if len(sys.argv) < 5:
        sys.stderr.write("usage: coacd_decompose.py <in.obj> <out.obj> <threshold> <max_hulls> [resolution] [mcts_iter]\n")
        return 2
    in_obj, out_obj = sys.argv[1], sys.argv[2]
    threshold = float(sys.argv[3])
    max_hulls = int(sys.argv[4])
    # LEGO parts are simple; a lighter preprocess remesh + shorter MCTS keeps the same
    # coarse decomposition far faster than CoACD's defaults (res 50 / 150 iters).
    resolution = int(sys.argv[5]) if len(sys.argv) > 5 else 30
    mcts_iter = int(sys.argv[6]) if len(sys.argv) > 6 else 100
    # decimate=True caps each output hull to max_ch_vertex vertices (built into CoACD).
    decimate = (sys.argv[7] == "1") if len(sys.argv) > 7 else False
    max_ch_vertex = int(sys.argv[8]) if len(sys.argv) > 8 else 256
    # real_metric=True interprets `threshold` in the mesh's real units (LDU here) instead
    # of CoACD's normalized concavity -- good for our fixed-scale LDraw parts.
    real_metric = (sys.argv[9] == "1") if len(sys.argv) > 9 else False

    try:
        import numpy as np
        import coacd
    except ImportError as e:
        sys.stderr.write("coacd/numpy not installed in this interpreter: {}\n".format(e))
        return 3

    verts, faces = read_obj(in_obj)
    if len(verts) < 4 or len(faces) < 4:
        sys.stderr.write("input mesh too small to decompose\n")
        return 4

    mesh = coacd.Mesh(np.array(verts, dtype=np.float64), np.array(faces, dtype=np.int32))
    # preprocess_mode="auto" remeshes non-manifold LDraw soup into a watertight input.
    result = coacd.run_coacd(
        mesh,
        threshold=threshold,
        max_convex_hull=max_hulls,
        preprocess_mode="auto",
        preprocess_resolution=resolution,
        mcts_iterations=mcts_iter,
        decimate=decimate,
        max_ch_vertex=max_ch_vertex,
        real_metric=real_metric,
    )
    # result is a list of (vertices, faces) numpy tuples.
    parts = [(np.asarray(v).tolist(), np.asarray(f).tolist()) for (v, f) in result]
    write_groups(out_obj, parts)
    sys.stderr.write("coacd: {} convex pieces\n".format(len(parts)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
