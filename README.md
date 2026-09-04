# C++ Route and Network Engine

A C++20 route engine for sparse, road-like networks: an immutable CSR graph
core, strict CSV input, Dijkstra and A\*, a deterministic synthetic network
generator, and a reproducible benchmark that compares the algorithms.

> **Status: in progress — Commits 1, 2 and 3 of four are complete.**
>
> **Working now:** the immutable CSR graph core, strict CSV loading, Dijkstra,
> A\* with a zero heuristic, A\* with a Euclidean heuristic, the metric contract
> that gates the Euclidean heuristic, the deterministic generator, the
> algorithm-comparison benchmark, and all four CLI commands.
>
> **Planned, and not present:** continuous integration, a committed reference
> benchmark result, and release preparation. Any benchmark numbers below were
> produced on one developer machine and are reproduced here as illustration,
> not as a portable result.

## Purpose

The project demonstrates modern C++ engineering, graph data structures,
shortest-path algorithms, correctness verification and honest performance
measurement, at a size that can be read end to end. It is deliberately not a
mapping platform.

## Build and test

Requires CMake 3.21+ and a C++20 compiler (GCC, Clang or AppleClang).
GoogleTest is fetched at configure time and used by the tests only; neither
`route_core` nor the CLI depends on it. There are no runtime dependencies
beyond the standard library.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

The `release` preset is the same with optimisation and `NDEBUG`; the
`asan-ubsan` preset adds AddressSanitizer and UndefinedBehaviourSanitizer and
assumes a GCC- or Clang-compatible compiler driver.

Warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`)
are always on. They are **not** errors in a local build; configure with
`-DROUTE_WARNINGS_AS_ERRORS=ON` to make them errors.

## Running it

The binary is built as `build/<preset>/route-engine`. A six-node example
network lives in `data/examples/`.

### Check a graph

```bash
./build/debug/route-engine validate \
  --nodes data/examples/tiny_nodes.csv \
  --edges data/examples/tiny_edges.csv
```

```
nodes file:       data/examples/tiny_nodes.csv
edges file:       data/examples/tiny_edges.csv
nodes:            6
arcs:             14
metric contract:  satisfied
  every arc weight is at least the straight-line distance between its endpoints,
  so Euclidean A* is admissible on this graph
  worst weight/distance ratio: 1
```

### Find a route

```bash
./build/debug/route-engine route \
  --nodes data/examples/tiny_nodes.csv \
  --edges data/examples/tiny_edges.csv \
  --from 1 --to 5 --algorithm astar-euclidean
```

```
algorithm:        astar-euclidean
from:             1
to:               5
route:            found
total cost:       1100
nodes on path:    4
path:             1 -> 2 -> 3 -> 5
statistics:
  nodes expanded:   5
  arcs examined:    10
  relaxations:      5
  queue pushes:     6
  queue pops:       5
  stale pops:       0
  max queue size:   3
```

`--algorithm` accepts `dijkstra`, `astar-zero` or `astar-euclidean`. On this
example Dijkstra expands 6 nodes and Euclidean A\* expands 5 for the same
route and the same cost. That is one query on a six-node graph, reported
because it is what the tool printed — it is not a benchmark, and no timing has
been measured.

### Generate a network

```bash
./build/release/route-engine generate \
  --width 40 --height 40 --seed 20250904 \
  --nodes /tmp/grid_nodes.csv --edges /tmp/grid_edges.csv
```

```
generated:        40 x 40 lattice
seed:             20250904
spacing:          100
jitter:           0.35
diagonal rate:    0.25
nodes:            1600
arcs:             7730
metric contract:  satisfied
```

Optional parameters, with their defaults: `--spacing 100` (metres between
lattice sites), `--jitter 0.35` (maximum displacement as a fraction of the
spacing, must be under 0.5 so neighbours cannot coincide), `--diagonal-rate
0.25` (probability that each of a cell's two diagonals is added). `generate`
never overwrites an existing file unless `--force` is given.

### Benchmark the algorithms

```bash
./build/release/route-engine benchmark \
  --nodes /tmp/grid_nodes.csv --edges /tmp/grid_edges.csv \
  --queries 200 --seed 7 --repetitions 5
```

```
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
dijkstra                    154733          753790        197261        185563       30830         145      0.007157
astar-zero                  154733          753790        197261        185563       30830         145      0.006929
astar-euclidean              25715          126904         49468         33127        7412         202      0.002513

node expansions relative to dijkstra:
  dijkstra          1.000
  astar-zero        1.000
  astar-euclidean   0.166
```

Use the `release` preset for anything you intend to time. Those figures come
from one machine, one build and one graph; see the caveats below.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success; route found where applicable |
| 1 | Valid query, but no route exists |
| 2 | Command-line usage error |
| 3 | CSV or graph data error, including a node id that is not in the node file |
| 4 | Euclidean A\* requested on a graph that violates the metric contract |
| 5 | The benchmark's algorithms disagreed with each other; no report was produced |

## Input format

Two files, one narrow format each. This is not a general CSV reader: comma
only, no quoting, no escapes, no dialect detection.

`nodes.csv`

```
id,x,y
1,0,0
2,300,0
```

`edges.csv`

```
source,target,weight
1,2,300
2,1,300
```

* `id` is a decimal unsigned integer, unique within the file. `x` and `y` are
  finite projected planar coordinates, in metres — not latitude and longitude.
* **Each edge row is exactly one directed arc.** A two-way street is two rows.
  Parallel arcs, self-loops and zero weights are accepted; negative and
  non-finite weights are not.
* A leading UTF-8 byte order mark is stripped, LF and CRLF are both accepted,
  spaces and tabs around a field are trimmed, blank lines and lines whose first
  non-whitespace character is `#` are skipped anywhere in the file.
* The header must be exact, including case and column order, and every data row
  must have exactly three fields.
* Numbers are parsed with `std::from_chars`, so parsing does not depend on the
  process locale.
* The first problem aborts the load and is reported with the file and the
  1-based line number, for example
  `edges.csv:12: weight must not be negative, found -3`. No partial graph is
  produced.

Internal node identifiers are assigned in order of appearance in `nodes.csv`.

## Compressed sparse row storage

A road-like network is sparse: most nodes have two to four outgoing arcs.
Storing adjacency as a vector of per-node vectors would mean one allocation per
node and a pointer chase on every expansion.

`Graph` instead keeps two arrays:

- `arcs` — every arc in the graph, in one contiguous block, sorted by
  `(source, target, weight)`;
- `offsets` — `node_count + 1` entries, where the outgoing arcs of node `u` are
  `arcs[offsets[u] .. offsets[u+1])`.

`arcs_from(u)` returns that slice as a `std::span<const Arc>`, so expanding a
node walks memory linearly. The canonical sort means two inputs describing the
same graph produce the same `Graph`, whatever order the arcs arrived in. Arc
indices are stored as 32-bit offsets, so the builder rejects node and arc counts
that those integers cannot represent rather than narrowing silently.

Construction and results are deterministic within one recorded build
environment. No claim is made that floating-point output is bit-identical across
different compilers or standard-library implementations.

## The metric contract

A\* returns an optimal route only when its heuristic never overestimates the
remaining cost. With planar coordinates and distance weights, that guarantee
reduces to a single property of the data — for every directed arc `(u, v)`:

```
weight(u, v) >= hypot(x_v - x_u, y_v - y_u)
```

If it holds for every arc, then `h(x) = distance(x, target)` is admissible (any
route from `x` is a chain of arcs at least as long as the straight line) and
consistent (`h(u) - h(v) <= distance(u, v) <= weight(u, v)`).

`Graph` computes this once at construction and stores the result as a
`MetricReport`: whether the whole graph passes, how many arcs violate the
contract, the endpoints of the first violating arc in canonical order, and the
smallest weight-to-distance ratio over arcs with a positive distance. Searches
read the stored report instead of repeating the O(arcs) scan.

The comparison uses the stored floating-point values with **no tolerance**. A
tolerance could accept a weight fractionally below the computed distance while
still claiming guaranteed admissibility, which is exactly the claim this check
exists to support. Zero-distance arcs — self-loops, or endpoints at identical
coordinates — pass whenever their weight is non-negative, which a built `Graph`
always guarantees, and are excluded from the ratio. A graph with no arcs,
including the empty graph and any one-node graph, passes vacuously.

**When the contract fails, `astar-euclidean` is refused with exit code 4.**
Dijkstra and `astar-zero` still run on such a graph and still return optimal
routes; only the heuristic that cannot be justified is withheld. There is no
override flag, and no result is ever labelled as possibly-suboptimal.

## Searching

Dijkstra and A\* are the same code: one search core parameterised on the
heuristic, with `dijkstra` and `astar-zero` both instantiating it with a
heuristic that returns zero. The priority queue orders entries by
`(priority, NodeId)`, so equal-cost alternatives are resolved the same way on
every run. Superseded queue entries are discarded lazily when they surface
rather than being removed by a decrease-key operation; the cost of that choice
is visible in the `stale pops` and `max queue size` counters instead of hidden.

Every search reports `nodes expanded`, `arcs examined`, `relaxations`,
`queue pushes`, `queue pops`, `stale pops` and `max queue size`. These counters
are exact and reproducible for a given graph and query; none of them is a
timing measurement, and `max queue size` counts queue entries, not bytes.

## The generated network

`generate` builds a jittered lattice, not a clean grid. Each of the
`width * height` sites is placed near its lattice position and displaced
independently in x and y by up to `jitter * spacing`. Every horizontal and
vertical neighbour pair is joined by **two arcs, one in each direction**, which
makes the graph strongly connected by construction with no repair step. Each
cell's two diagonals are then offered independently and accepted with
probability `diagonal-rate`, again as two arcs.

Node `r * width + c` gets external identifier `r * width + c + 1`, so
identifiers run from 1 in row-major order.

**Every arc weight is exactly the Euclidean distance between its endpoints**,
so the metric contract holds with equality and a generated graph is always
usable by Euclidean A\*. `generate` verifies that on the finished graph rather
than assuming it.

### Determinism

The pseudo-random stream is **SplitMix64**, written out in full in
`include/route/random.hpp`. Neither `std::rand` nor the
`std::uniform_*_distribution` templates are used: the standard fixes the
engines but not the distributions, so their output legitimately differs between
standard libraries and a generator built on them would only be deterministic on
one machine.

The draw order is fixed and documented: all coordinates first, in row-major
order with x before y, then two draws per cell for the diagonals. The diagonal
draws happen whether or not the diagonal is accepted, so changing
`--diagonal-rate` does not shift the stream and the node coordinates for a
given seed are the same at every rate.

Numbers are written with `std::to_chars`, which is locale-independent and emits
the shortest text that reads back as the identical `double`. The same
parameters therefore produce byte-identical CSV files, and a generated file
reloads into a bit-identical graph — which is what keeps the metric contract
satisfied after a round trip through disk. That guarantee is within one build:
it assumes `std::hypot` returns the same value when the file is read as it did
when it was written.

## Benchmark methodology

`benchmark` compares Dijkstra, zero-heuristic A\* and Euclidean A\* on one graph
and one query set. The fairness rules are enforced by the harness, not left to
the person running it:

* The graph must satisfy the metric contract, or the run is refused with exit
  code 4 — a comparison that excluded Euclidean A\* would not be the comparison
  being claimed.
* One deterministic query set is built from `--seed` and **every algorithm and
  every repetition sees exactly that set, in that order**. Each query draws a
  source and then a non-zero offset, so source and target are always distinct.
* Every algorithm gets one warm-up pass, excluded from every reported figure.
* The warm-up doubles as the agreement pass, so the cross-checking work is
  never inside a timed region.
* Every query is checked: all three algorithms must agree on whether a route
  exists and, when it does, on its cost. Disagreement aborts with exit code 5
  and **no report is printed** — a comparison that might be wrong is worse than
  no comparison.
* The execution order rotates by one algorithm on each repetition, so none of
  them always runs first and always pays the cold-cache cost.
* Counters must be identical in every repetition; a difference means the search
  is not deterministic and also aborts with exit code 5.

### What is measured

**Node expansions are the primary comparison.** They are exact, reproducible
for a given graph and query set, and independent of the machine, the compiler
and the build type. Arcs examined, queue pushes and pops, stale pops and the
maximum queue size are reported alongside them, all exact for the same reason.
The maximum queue size counts queue *entries*, including superseded ones; it is
not a memory measurement and no memory measurement is made.

**Elapsed time is a secondary observation.** The median across repetitions is
reported for each algorithm, and that is all it is: a description of one build
on one machine at one moment. It is not comparable with any other machine,
compiler, build type or run, and **no test asserts anything about timing**.

Nothing here claims that A\* always expands fewer nodes than Dijkstra. Whether
it does, and by how much, depends on the graph and on the query; the numbers
above describe one lattice and one query set.

## Verification

The test suite covers the graph core, the metric contract, every documented
parser rule, and the searches. Search correctness is cross-checked against a
deliberately naive Bellman–Ford reference in `tests/support/`, which shares no
code with the search core, on every source/target pair of several fixed small
graphs; every returned path is re-walked against the arc array and its cost
re-summed independently of the search that produced it.

Generator and benchmark coverage adds: the SplitMix64 stream checked against
reference values from an independent implementation; repeatability of both the
graph and the CSV bytes; lattice node and arc counts against the geometry;
strong connectivity through the backbone alone; every weight equal to the
Euclidean distance exactly; the metric contract across several shapes and
seeds; a full CSV round trip compared bit for bit; parameter rejection; and
deterministic, in-range, non-degenerate query generation. No test asserts a
timing result.
