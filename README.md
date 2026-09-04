# C++ Route and Network Engine

A C++20 route-engine project under construction, targeting sparse, road-like
networks. Loading, Dijkstra, A\* and a two-command CLI are working; a
deterministic graph generator and a reproducible benchmark comparing the
algorithms are planned for later milestones.

> **Status: in progress — Commits 1 and 2 of four are complete.**
>
> **Working now:** the immutable CSR graph core, strict CSV loading,
> Dijkstra, A\* with a zero heuristic, A\* with a Euclidean heuristic, the
> metric contract that gates the Euclidean heuristic, and the `validate` and
> `route` commands.
>
> **Planned, and not present:** the deterministic graph generator, the
> benchmark harness and its results, and continuous integration. No
> performance measurement of any kind has been made or is claimed.

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

`generate` and `benchmark` are named in the usage text as planned subcommands
and currently exit with a usage error.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success; route found where applicable |
| 1 | Valid query, but no route exists |
| 2 | Command-line usage error |
| 3 | CSV or graph data error, including a node id that is not in the node file |
| 4 | Euclidean A\* requested on a graph that violates the metric contract |

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

## Verification

The test suite covers the graph core, the metric contract, every documented
parser rule, and the searches. Search correctness is cross-checked against a
deliberately naive Bellman–Ford reference in `tests/support/`, which shares no
code with the search core, on every source/target pair of several fixed small
graphs; every returned path is re-walked against the arc array and its cost
re-summed independently of the search that produced it.
