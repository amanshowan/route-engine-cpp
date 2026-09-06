# C++ Route and Network Engine

[![CI](https://github.com/amanshowan/route-engine-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/amanshowan/route-engine-cpp/actions/workflows/ci.yml)

Shortest-path routing over sparse, road-like networks: about 2,500 lines of
C++20 backed by 1,900 lines of tests, with no runtime dependencies beyond the
standard library. It loads a network from CSV, answers route queries with
**Dijkstra** or **A\***, generates reproducible synthetic networks, and runs a
benchmark that compares the algorithms **and refuses to publish a comparison it
cannot verify**.

> **Status: v1.0 complete.** All four milestones are implemented, and the full
> suite passes locally in debug, release and sanitizer builds.
>
> GitHub Actions passed on release commit `0dc8f65`: GCC and Clang debug builds
> with warnings as errors, the Clang AddressSanitizer +
> UndefinedBehaviorSanitizer job, and the release smoke job that exercises
> `generate`, `validate`, `route` and `benchmark` end to end. The project is
> tagged and published as `v1.0.0`.

## What is worth looking at

| | |
|---|---|
| **The A\* correctness gate** | A\* is only optimal if its heuristic never overestimates. That guarantee is reduced to one checkable property of the data, verified in O(arcs) at load time, and **Euclidean A\* is refused outright** on a graph that fails it. No override flag exists. → [The metric contract](#the-metric-contract) |
| **A benchmark that can fail** | All three algorithms must agree on route existence and cost for every query, or the run aborts and prints **nothing**. Fairness rules — shared query set, excluded warm-up, rotated execution order, counter determinism — are enforced by the harness, not by the person running it. → [Benchmark fairness](#benchmark-fairness) |
| **An independent correctness reference** | Search results are cross-checked against a deliberately naive Bellman–Ford implementation that shares no code with the search core, on every source/target pair of several fixed graphs; every path returned is re-walked against the arc array and its cost re-summed. → [Verification](#verification) |
| **Determinism taken seriously** | The PRNG is written out in full rather than delegated to `std::uniform_*_distribution`, whose output is not fixed by the standard. Numbers are parsed and written with `std::from_chars`/`std::to_chars`, so nothing depends on the process locale. → [Generator determinism](#generator-determinism) |
| **Honest measurement** | Node expansions are the headline metric because they are an exact count rather than a sampled measurement, and are deterministic for a given build and input. Timings are labelled as local observations, no test asserts one, and no memory figure is claimed because none is measured. → [benchmarks/](benchmarks/2026-09-04-macos-arm64-appleclang21.md) |

## Quick start

Requires CMake 3.21+ and a C++20 compiler (GCC, Clang or AppleClang).
GoogleTest is fetched at configure time and used by the tests only; neither
`route_core` nor the CLI links against it.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Presets: `debug`, `release` (`-O2 -DNDEBUG` — use this for anything you intend
to time), and `asan-ubsan` (AddressSanitizer + UndefinedBehaviorSanitizer,
assumes a GCC- or Clang-compatible driver).

Warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`)
are always on. They are **not** errors in a local build; CI configures with
`-DROUTE_WARNINGS_AS_ERRORS=ON`.

## Using it

The binary is `build/<preset>/route-engine`. A six-node example network lives in
`data/examples/`.

### `validate` — check a network

```bash
./build/debug/route-engine validate \
  --nodes data/examples/tiny_nodes.csv --edges data/examples/tiny_edges.csv
```

```
nodes:            6
arcs:             14
metric contract:  satisfied
  every arc weight is at least the straight-line distance between its endpoints,
  so Euclidean A* is admissible on this graph
  worst weight/distance ratio: 1
```

### `route` — answer one query

```bash
./build/debug/route-engine route \
  --nodes data/examples/tiny_nodes.csv --edges data/examples/tiny_edges.csv \
  --from 1 --to 5 --algorithm astar-euclidean
```

```
algorithm:        astar-euclidean
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

`--algorithm` accepts `dijkstra`, `astar-zero` or `astar-euclidean`.

### `generate` — build a reproducible synthetic network

```bash
./build/release/route-engine generate \
  --width 40 --height 40 --seed 20250904 \
  --nodes /tmp/nodes.csv --edges /tmp/edges.csv
```

Optional, with defaults: `--spacing 100` (metres between lattice sites),
`--jitter 0.35` (maximum displacement as a fraction of the spacing; must be
under 0.5 so neighbours cannot coincide), `--diagonal-rate 0.25` (probability
each of a cell's two diagonals is added). An existing file is never overwritten
unless `--force` is given.

### `benchmark` — compare the algorithms

```bash
./build/release/route-engine benchmark \
  --nodes /tmp/nodes.csv --edges /tmp/edges.csv \
  --queries 200 --seed 7 --repetitions 5
```

On the 40×40 network above, over 200 queries: Dijkstra and zero-heuristic A\*
each expanded **154,733** nodes, Euclidean A\* expanded **25,715** — about one
sixth — with all three algorithms agreeing on route existence and cost.

The full record, with environment, exact commands and complete raw output, is
committed at
[`benchmarks/2026-09-04-macos-arm64-appleclang21.md`](benchmarks/2026-09-04-macos-arm64-appleclang21.md).

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success; route found where applicable |
| 1 | Valid query, but no route exists |
| 2 | Command-line usage error |
| 3 | CSV, graph data or output-file error |
| 4 | Euclidean A\* requested on a graph that violates the metric contract |
| 5 | The benchmark's algorithms disagreed with each other; no report was produced |

## Architecture

```
route_cli ──┐
            ▼
        route_core ◄── route_tests
     graph · io · search
```

`route_core` is a static library with no globals, no threads and no I/O except
through an injected stream. The CLI is a thin driver over exactly the code the
tests exercise.

### Compressed sparse row storage

A road-like network is sparse: most nodes have two to four outgoing arcs.
Adjacency as a vector of per-node vectors would mean one allocation per node and
a pointer chase on every expansion. `Graph` instead keeps two arrays:

- `arcs` — every arc in one contiguous block, sorted by `(source, target, weight)`;
- `offsets` — `node_count + 1` entries, where node `u`'s outgoing arcs are
  `arcs[offsets[u] .. offsets[u+1])`.

`arcs_from(u)` returns that slice as a `std::span<const Arc>`, so expanding a
node walks memory linearly. The canonical sort means two inputs describing the
same graph produce the same `Graph` whatever order the arcs arrived in. Arc
indices are 32-bit offsets, so the builder rejects node and arc counts those
integers cannot represent rather than narrowing silently.

`GraphBuilder` accumulates and validates; `Graph` is immutable and move-only,
built once and queried many times, with no invalid state to check on access.

### Searching

Dijkstra and A\* are **the same code**: one search core parameterised on the
heuristic, with `dijkstra` and `astar-zero` both instantiating it with a
heuristic that returns zero. The priority queue orders entries by
`(priority, NodeId)`, so equal-cost alternatives resolve identically on every
run. Superseded entries are discarded lazily when they surface rather than
removed by a decrease-key operation; the cost of that choice is visible in the
`stale pops` and `max queue size` counters instead of hidden.

Every search reports nodes expanded, arcs examined, relaxations, queue pushes
and pops, stale pops, and maximum queue size. All are exact and reproducible for
a given graph and query. None is a timing measurement, and `max queue size`
counts queue *entries*, not bytes.

## Input format

Two files, one narrow format each. Deliberately not a general CSV reader: comma
only, no quoting, no escapes, no dialect detection.

```
nodes.csv          edges.csv
id,x,y             source,target,weight
1,0,0              1,2,300
2,300,0            2,1,300
```

* `id` is a decimal unsigned integer, unique within the file. `x` and `y` are
  finite projected planar coordinates in metres — not latitude and longitude.
* **Each edge row is exactly one directed arc.** A two-way street is two rows.
  Parallel arcs, self-loops and zero weights are accepted; negative and
  non-finite weights are not.
* A leading UTF-8 byte order mark is stripped; LF and CRLF are both accepted;
  spaces and tabs around a field are trimmed; blank lines and lines whose first
  non-whitespace character is `#` are skipped anywhere in the file.
* The header must be exact, including case and column order, and every data row
  must have exactly three fields.
* Numbers are parsed with `std::from_chars`, so parsing does not depend on the
  process locale. (`std::stod` would read `1.5` as `1` under a comma-decimal
  locale — a real and quiet bug.)
* The first problem aborts the load and is reported with file and 1-based line
  number, e.g. `edges.csv:12: weight must not be negative, found -3`. No partial
  graph is produced.

Internal node identifiers are assigned in order of appearance in `nodes.csv`.

## The metric contract

A\* returns an optimal route only when its heuristic never overestimates the
remaining cost. With planar coordinates and distance weights, that guarantee
reduces to one property of the data — for every directed arc `(u, v)`:

```
weight(u, v) >= hypot(x_v - x_u, y_v - y_u)
```

If it holds everywhere, then `h(x) = distance(x, target)` is **admissible** (any
route from `x` is a chain of arcs at least as long as the straight line) and
**consistent** (`h(u) - h(v) <= distance(u, v) <= weight(u, v)`), and consistency
is what licenses never re-expanding a settled node.

`Graph` computes this once at construction and stores a `MetricReport`: whether
the graph passes, how many arcs violate it, the first violating arc's endpoints
in canonical order, and the smallest weight-to-distance ratio over arcs with a
positive distance. Searches read the stored report rather than rescanning.

The comparison uses the stored floating-point values with **no tolerance**. A
tolerance could accept a weight fractionally below the computed distance while
still claiming guaranteed admissibility — exactly the claim this check exists to
support. Zero-distance arcs (self-loops, coincident endpoints) pass whenever
their weight is non-negative, which a built `Graph` always guarantees, and are
excluded from the ratio. A graph with no arcs passes vacuously.

**When the contract fails, `astar-euclidean` is refused with exit code 4.**
Dijkstra and `astar-zero` still run on that graph and still return optimal
routes; only the heuristic that cannot be justified is withheld. There is no
override flag, and no result is ever labelled possibly-suboptimal.

## The generated network

`generate` builds a jittered lattice, not a clean grid: each of the
`width × height` sites is displaced independently in x and y by up to
`jitter × spacing`. Every horizontal and vertical neighbour pair is joined by
**two arcs, one in each direction**, which makes the graph strongly connected by
construction with no repair step. Each cell's two diagonals are then offered
independently and accepted with probability `diagonal-rate`.

Node `r × width + c` gets external identifier `r × width + c + 1`, so
identifiers run from 1 in row-major order.

**Every arc weight is exactly the Euclidean distance between its endpoints**, so
the metric contract holds with equality and a generated graph is always usable
by Euclidean A\*. `generate` verifies that on the finished graph rather than
assuming its own contract.

### Generator determinism

The pseudo-random stream is **SplitMix64**, written out in full in
`include/route/random.hpp`. Neither `std::rand` nor the
`std::uniform_*_distribution` templates are used: the standard fixes the engines
but not the distribution algorithms, so the same engine and the same seed can
legitimately produce different values under different standard-library
implementations. A generator built on them would therefore not give portable
deterministic output.

The draw order is fixed and documented: all coordinates first, row-major, x
before y; then two draws per cell for the diagonals. The diagonal draws happen
whether or not the diagonal is accepted, so changing `--diagonal-rate` does not
shift the stream — node coordinates for a given seed are the same at every rate.

Numbers are written with `std::to_chars`, which is locale-independent and emits
the shortest text that reads back as the identical `double`. The same parameters
therefore produce byte-identical CSV files, and a generated file reloads into a
bit-identical graph — which is what keeps the metric contract satisfied after a
round trip through disk. That guarantee holds within one build environment: it
assumes `std::hypot` returns the same value when the file is read as when it was
written.

## Benchmark fairness

`benchmark` compares Dijkstra, zero-heuristic A\* and Euclidean A\* on one graph
and one query set. The rules are enforced by the harness:

* The graph must satisfy the metric contract, or the run is refused (exit 4) — a
  comparison that quietly excluded Euclidean A\* would not be the comparison
  being claimed.
* One deterministic query set is built from `--seed`, and **every algorithm and
  every repetition sees exactly that set in that order**. Each query draws a
  source then a non-zero offset, so source and target are always distinct.
* Every algorithm gets one warm-up pass, excluded from every reported figure.
* The warm-up doubles as the agreement pass, so cross-checking work is never
  inside a timed region.
* Every query is checked: all three algorithms must agree on whether a route
  exists and, when it does, on its cost. Disagreement aborts with exit code 5
  and **no report is printed** — a comparison that might be wrong is worse than
  no comparison.
* Execution order rotates by one algorithm on each repetition, so none of them
  always runs first and always pays the cold-cache cost.
* Counters must be identical in every repetition; a difference means the search
  is not deterministic and also aborts with exit code 5.

### What is measured

**Node expansions are the primary comparison** — an exact count rather than a
sampled measurement, and deterministic for a given build against an identical
graph and query set, so repeated runs reproduce them precisely. That is a much
firmer footing than wall-clock timing, but it is not a cross-toolchain
guarantee: a different compiler, standard library or architecture may produce
slightly different generated floating-point values, and a search over different
values can settle a different number of nodes. Arcs examined, queue pushes and
pops, stale pops and maximum queue size are reported alongside them and are
exact in the same sense.

**Elapsed time is a secondary observation.** The median across repetitions is
reported, and that is all it is: one build, one machine, one moment. It is not
comparable with any other machine, compiler, build type or run, and **no test
asserts anything about timing**. CI runs a benchmark only as a smoke test;
timings from shared runners are never published.

Nothing here claims A\* always expands fewer nodes than Dijkstra. Whether it
does, and by how much, depends on the graph and the queries.

## Verification

104 tests across six files, run in debug, release and sanitizer builds.

* **Graph core and metric contract** — canonical ordering independent of
  insertion order, CSR row partitioning, duplicate and unset nodes, invalid
  endpoints, negative and non-finite weights, non-finite coordinates,
  out-of-range access, integer-capacity limits; the contract boundary tested at
  one ULP in both directions.
* **Parser** — one case per documented rule, each asserting the error message
  *and* the line number.
* **Search** — hand-computed answers; source equals target; unreachable targets;
  directed asymmetry; deterministic tie-breaking; lazy-deletion stale pops;
  contract refusal; counter invariants.
* **Independent cross-check** — a naive Bellman–Ford reference in
  `tests/support/` that shares no code with the search core, run on every
  source/target pair of several fixed graphs, plus path re-validation against
  the arc array with the cost re-summed.
* **Generator** — SplitMix64 against reference values from an independent
  implementation; byte-identical repeat generation; lattice counts against the
  geometry; strong connectivity through the backbone alone; every weight equal
  to `hypot` exactly; the contract across several shapes and seeds; a full CSV
  round trip compared bit for bit; parameter rejection.
* **Benchmark** — deterministic, in-range, non-degenerate query generation;
  agreement checks; refusals. **No test asserts a timing result.**

### Continuous integration

`.github/workflows/ci.yml` runs on pushes to `main` and on pull requests, with
`contents: read` permission and no third-party actions:

| Job | What it does |
|---|---|
| `gcc / debug`, `clang / debug` | Configure with `-DROUTE_WARNINGS_AS_ERRORS=ON`, build, run the full suite |
| `clang / asan+ubsan` | The `asan-ubsan` preset over the whole suite |
| `gcc / release smoke` | Release build and tests, then `generate` → `validate` → `route` → `benchmark` end to end |

## Limitations and non-goals

* **The graphs are synthetic.** A jittered lattice is not a real road network:
  no OSM, GeoJSON or shapefile import, and no plan to add one. Benchmark results
  describe this generated family, not real-world routing.
* **Planar coordinates only.** No latitude/longitude, no geodesic distance.
* **No memory measurement.** `max queue size` counts queue entries and is a
  frontier proxy, nothing more.
* **Timings are not portable.** See every caveat above.
* Deliberately absent: web or graphical interface, database, authentication,
  real-time traffic, turn restrictions, dynamic graph mutation, parallel search,
  and arbitrary CSV dialects.

## Future work

Named because leaving them unmentioned would imply this is the state of the art;
each is a project in itself and none is started.

* **Bidirectional search**, **ALT** and **contraction hierarchies** — all
  substantially faster than plain A\* on road networks.
* Real-map import, and travel-time weights (the metric contract generalises to
  `weight >= k · distance` for a minimum cost-per-metre `k`).
* A Windows/MSVC leg in CI, and larger benchmark sizes with per-stratum
  reporting by straight-line query distance.
