# C++ Route and Network Engine

A C++20 route-engine project under construction, targeting sparse, road-like
networks. The immutable graph core is in place; Dijkstra, A\*, a deterministic
graph generator and a reproducible benchmark comparing the two algorithms are
planned for later milestones.

> **Status: in progress.**
> This repository currently contains **Commit 1 — the graph foundation only**:
> node and arc types, `GraphBuilder`, the immutable CSR `Graph`, metric-contract
> checking, the build system, and unit tests.
>
> There is **no CSV loading, no Dijkstra, no A\*, no generator, no benchmark, no
> continuous integration and no performance result** in the repository yet. The
> `route-engine` executable is a stub: it prints usage and exits with code `2`.

## Purpose

The project demonstrates modern C++ engineering, graph data structures,
shortest-path algorithms, correctness verification and honest performance
measurement, at a size that can be read end to end. It is deliberately not a
mapping platform.

## Planned v1 capabilities

All of the following are **planned**, not present:

- Strict, narrowly documented CSV input for nodes and directed arcs.
- Dijkstra, A\* with a zero heuristic (as a control), and A\* with a Euclidean
  heuristic, permitted only on graphs that satisfy the metric contract below.
- Search diagnostics: nodes expanded, arcs examined, relaxations, queue pushes
  and pops, stale pops, and maximum queue size.
- A deterministic, seeded spatial graph generator.
- A reproducible benchmark over 1,000 / 10,000 / 100,000-node generated graphs.
- A four-subcommand CLI: `validate`, `route`, `generate`, `benchmark`.

## Implemented now

- `NodeId`, `ExternalId`, `Coord`, `Weight`, `Arc` (`include/route/types.hpp`).
- `GraphBuilder`: validated accumulation of nodes and directed arcs, canonical
  arc ordering, CSR construction (`include/route/builder.hpp`).
- `Graph`: immutable, move-only, read-only accessors returning
  `std::span<const Arc>` (`include/route/graph.hpp`).
- Metric-contract checking, computed once per graph
  (`include/route/metric.hpp`).
- Unit tests for the graph core and the metric contract.

## Build and test

Requires CMake 3.21+ and a C++20 compiler (GCC, Clang or AppleClang).
GoogleTest is fetched at configure time and is used by the tests only; neither
`route_core` nor the CLI depends on it.

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
`-DROUTE_WARNINGS_AS_ERRORS=ON` to make them errors, as CI will.

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
smallest weight-to-distance ratio over arcs with a positive distance. Later
searches read the stored report instead of repeating the O(arcs) scan.

The comparison uses the stored floating-point values with **no tolerance**. A
tolerance could accept a weight fractionally below the computed distance while
still claiming guaranteed admissibility, which is exactly the claim this check
exists to support. Zero-distance arcs — self-loops, or endpoints at identical
coordinates — pass whenever their weight is non-negative, which a built `Graph`
always guarantees, and are excluded from the ratio. A graph with no arcs,
including the empty graph and any one-node graph, passes vacuously.

When the contract fails, the planned behaviour is to refuse Euclidean A\* rather
than to return a route that cannot be shown to be optimal.
