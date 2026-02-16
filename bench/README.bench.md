# Benchmark Harness

This folder documents the **benchmark and correctness harness** used by the
Order Matching Engine (OME).

The harness serves two purposes:
1. **Deterministic correctness validation** using a trace–replay model
2. **Single-threaded performance benchmarking** with controlled latency
   instrumentation

The two modes are strictly separated to avoid cross-contamination of results.

---

## Deterministic Correctness Validation

Correctness is validated using a **trace–replay approach** designed to prove
deterministic behavior and absence of hidden state.

### Validation Flow

1. **Golden execution**
   - Orders are processed normally by the engine
   - All state-changing actions (ADD / CANCEL / TRADE / MODIFY) are recorded
     as structured events with a deterministic sequence number

2. **Replay execution**
   - The recorded trace is replayed into a fresh engine instance
   - The same operations are executed in the same logical order

3. **Snapshot comparison**
   - Final order-book snapshots (aggregated by price level) from golden
     and replay runs are compared textually

Matching snapshots guarantee:
- deterministic execution
- absence of hidden or implicit state
- correctness across all supported order types

Generated artifacts (traces, events, snapshots) are produced **locally**
during correctness runs and are intentionally **not committed** to the repository.

---

## Events and Snapshots

- **Event logs** capture every state transition in execution order
- **Snapshots** represent the final order-book state at price-level granularity
- A monotonic sequence number (`seq`) is used instead of timestamps to ensure
  reproducibility across runs and platforms

Event logging is enabled only during correctness runs and disabled entirely
during performance benchmarking.

---

## Benchmark Scope

The benchmark harness is designed to:
- validate deterministic correctness
- measure single-threaded throughput and latency
- provide a realistic mixed workload representative of exchange-style order flow

It is **not** intended to:
- compare different engines
- claim exchange-grade latency
- benchmark kernel-bypass or networking stacks

---

## Allocation Model

The benchmark uses a fixed-capacity Orderpool to avoid dynamic allocation during matching.

Pool size is derived from the workload scenario to ensure no allocation failures during correctness or performance runs.

This removes allocator noise from latency measurements and improves reproducibility across runs.

---

## Workloads

Each benchmark run consists of the following phases:

### Warmup
- Small number of GTC orders
- Stabilizes caches and branch predictors

### Bulk Insert
- Sequential insertion of a large number of GTC orders
- Builds book depth and price-level distribution

### Random Ops (primary workload)
A mixed workload simulating realistic order flow:
- ~40% best-price queries (`GetBestBidPrice` / `GetBestAskPrice`)
- ~25% cancels of random live orders
- ~5% explicit `MatchOrders()` calls
- Remaining operations:
  - new order adds (mostly GTC, with occasional Market / IOC / FOK)
  - order modifications

### Best-Bid Stress
- Tight loop of 200k `GetBestBidPrice()` calls
- Measures hot-path query latency in isolation

---

## Performance Scenarios

Current performance runs include:
- **100k-100k**: 100k bulk insert + 100k random operations
- **200k-200k**: 200k bulk insert + 200k random operations

The primary performance harness is single-threaded to eliminate lock contention and scheduling noise.

A separate multi-threaded integration benchmark (Feed → SPSC → Engine) is provided to evaluate end-to-end latency and queue behavior under controlled pacing.

---

## Latency Measurement Methodology

Latency instrumentation is implemented **entirely in the benchmark harness**,
not in the matching engine.

- Latency sampling is enabled **only in performance mode**
- Correctness (trace–replay) runs perform no latency measurement
- A single monotonic clock (`std::chrono::steady_clock`) is used
- Each sampled **logical operation** produces exactly one latency sample
- No trace generation, event logging, or I/O occurs in the hot path
- Instrumentation does not affect execution order or replay determinism

### Percentiles

Latency percentiles (p50 / p90 / p99) are computed using
`std::nth_element` (partial sort) on the collected latency vector.

This avoids the O(n log n) cost and memory overhead of a full sort while still
providing accurate percentile values with minimal measurement overhead.

### Histograms

Latency histograms are populated using predefined buckets
(100 ns up to 10 ms+) to support detailed tail-latency analysis.

---

## Output Artifacts

Performance runs produce the following outputs:

- `bench_results.csv`  
  Summary metrics per phase and scenario (ops, total time, avg latency)

- `latency_random_ops_<scenario>.csv`  
  Full latency histogram and percentiles for each scenario (not committed)

- `latency_summary.csv`  
  Consolidated p50 / p90 / p99 across scenarios

Console output additionally reports:
- per-phase timings
- throughput
- final latency percentiles

---

## Test Environment

Performance measurements were collected under the following configuration:

- CPU: Intel Core i5-4430 (4 cores, 3.0 GHz base, 3.2 GHz turbo)
- Architecture: x86_64
- L1 Cache: 128 KiB (data) / 128 KiB (instruction)
- L2 Cache: 1 MiB
- L3 Cache: 6 MiB
- Threads per core: 1 (SMT disabled)
- NUMA nodes: 1
- OS: Linux (Ubuntu-based)
- Compiler: g++ (C++20)
- Build flags: -O3 -march=native -DNDEBUG -pthread
- Execution mode: single-threaded

---

## Offline Analysis

Offline analysis is intentionally separated from the benchmark harness.

- CSV outputs are consumed by a small Python script under `analysis/`
- Plots are generated for p50 and p99 latency versus workload size
- No analysis or plotting code is present in the engine or harness

---

## Notes

- Performance results were collected using -O3 -march=native -DNDEBUG on a single-threaded Linux x86_64 system.
- Event logging is disabled in performance mode to eliminate observer overhead.
- Tail latency is primarily influenced by:
  - order-book depth (number of price levels)
  - occasional deep sweeps during aggressive matching
  - cleanup of non-GTC orders (IOC / Market conversions)