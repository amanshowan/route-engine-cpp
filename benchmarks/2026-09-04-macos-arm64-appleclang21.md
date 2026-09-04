# Reference benchmark — 2026-09-04

> **Timings in this file are a local observation, not a portable result.**
> They describe one build of one commit on one machine at one moment. They are
> not comparable with any other machine, compiler, build type or run, and
> nothing in the test suite asserts them. The result to rely on is the
> **node-expansion count**: an exact count rather than a sampled measurement,
> reproducible for the recorded build against this identical graph and query
> set. The two local runs recorded below reproduced the same counts. Identical
> counts on a different toolchain are expected in many cases but are not
> guaranteed, because the generated floating-point coordinates and the
> arithmetic performed on them can differ.

## What was tested

| | |
|---|---|
| Commit | `82ea2a6` |
| Date | 2026-09-04 |
| Operating system | macOS 26.6.2 (Darwin 25.6.0) |
| Architecture | arm64 (Apple M4, 10 cores) |
| Compiler | AppleClang 21.0.0 (clang-2100.1.1.101) |
| Build type | Release (`-O2 -DNDEBUG`, `release` preset) |
| CMake | 4.4.3 |

## Commands

```bash
# 1. Build the release preset from a clean directory.
cmake --preset release
cmake --build --preset release

# 2. Generate the reference network: 40x40 lattice, graph seed 20250904,
#    default spacing (100), jitter (0.35) and diagonal rate (0.25).
mkdir -p /tmp/route-engine-ref
./build/release/route-engine generate \
  --width 40 --height 40 --seed 20250904 \
  --nodes /tmp/route-engine-ref/nodes.csv \
  --edges /tmp/route-engine-ref/edges.csv

# 3. Benchmark: 200 queries, query seed 7, 5 repetitions.
./build/release/route-engine benchmark \
  --nodes /tmp/route-engine-ref/nodes.csv \
  --edges /tmp/route-engine-ref/edges.csv \
  --queries 200 --seed 7 --repetitions 5
```

The generated CSV files are not committed: they are a deterministic function of
the parameters above, so regenerating them is cheaper and more trustworthy than
storing them. Generation was run twice into separate directories before this
record was made and the two pairs of files were byte-identical (`cmp` clean):

```
2ff528812814f0e9ca31fbaf639a15b106619925b7822ab7e21f8b6a8b1edf0c  nodes.csv
6d28ba95ee4e2b8b4122d3608538311ed92453fa00c9722addec7e806e0972d8  edges.csv
```

Those digests are for this build. The generator's determinism guarantee is
within one build environment; a different compiler or standard library may
produce different low-order bits and therefore different digests. The graph's
structure -- node count, arc count, which pairs are connected -- is fixed by
the parameters and does not depend on those low-order bits. The node-expansion
counts are expected to match in many such cases, but they are not guaranteed
to: they are computed from the generated coordinates and weights, so different
floating-point values or different arithmetic can change which nodes a search
settles.

## Primary result: node expansions

An exact count rather than a sampled measurement, and reproducible for the
recorded build against this identical graph and query set. Over 200 queries on
the 1600-node network:

| Algorithm | Nodes expanded | Relative to Dijkstra |
|---|---:|---:|
| `dijkstra` | 154,733 | 1.000 |
| `astar-zero` | 154,733 | 1.000 |
| `astar-euclidean` | 25,715 | 0.166 |

`astar-zero` matching `dijkstra` exactly is the control: both instantiate the
same search core with a heuristic that returns zero, so any difference between
them would be a defect rather than a finding. The Euclidean heuristic expanded
about one sixth as many nodes **on this graph and this query set**. That is not
a general claim: how much A\* helps, and whether it helps at all, depends on the
graph and on the queries.

## Complete raw output

```
nodes file:       /tmp/route-engine-ref/nodes.csv
edges file:       /tmp/route-engine-ref/edges.csv
nodes:            1600
arcs:             7730
metric contract:  satisfied
queries:          200 (seed 7)
repetitions:      5
warm-up:          1 pass per algorithm, excluded from every figure below
order:            rotated by one algorithm on each repetition
routes found:     200 of 200
agreement:        every algorithm agrees on route existence and cost

totals over 200 queries, per algorithm
algorithm                 expanded            arcs        pushes          pops       stale   max queue      median s
dijkstra                    154733          753790        197261        185563       30830         145      0.007008
astar-zero                  154733          753790        197261        185563       30830         145      0.006648
astar-euclidean              25715          126904         49468         33127        7412         202      0.002561

node expansions relative to dijkstra:
  dijkstra          1.000
  astar-zero        1.000
  astar-euclidean   0.166

Node expansions are the primary comparison. They are exact, reproducible for a
given graph and query set, and independent of this machine.

Elapsed time is a secondary observation only. It describes this build on this
machine at this moment and is not comparable with any other machine, compiler,
build type or run; no test asserts anything about it. These figures also do not
claim that A* always expands fewer nodes than Dijkstra -- that depends on the
graph and on the query, and this is one graph and one query set.
```

The block above is the raw output as printed by commit `82ea2a6`, reproduced
unchanged. Its footer line "independent of this machine" was meant in the sense
of "not a noisy wall-clock measurement" -- an exact count, not a sample. That
phrasing was tightened before v1.0 to "deterministic for this build and this
exact input", because read literally it could suggest a cross-toolchain
guarantee that this project does not make. The current binary prints the
tightened wording; the counts themselves are unchanged.

## Reading the secondary figures

`median s` is the median wall-clock time, across the 5 repetitions, for one
full pass over all 200 queries. Each algorithm also ran one warm-up pass that
is excluded from every number here, and the execution order rotated by one
algorithm on each repetition so none of them always ran first.

`max queue` is the largest priority-queue size observed on any single query,
counted in **entries** — superseded entries included, because the search uses
lazy deletion rather than decrease-key. It is a frontier proxy, not a memory
measurement; no memory measurement is made anywhere in this project.
