# Order Matching Engine (C++)

A deterministic **order matching engine** implemented in modern C++, supporting core exchange-style order types and validated using a **deterministic trace–replay correctness harness**.

This project focuses on **correct matching semantics, determinism, and clean system design**, rather than exchange-specific networking or kernel-bypass optimizations.

---

## Features

### Supported Order Types

- **Good-Till-Cancel (GTC)** — rests in the book until filled or canceled
- **Market** — executes immediately by sweeping available opposite-side liquidity (implemented internally via aggressive IOC conversion)
- **Immediate-Or-Cancel (IOC)** — executes immediately; unfilled quantity is canceled
- **Fill-Or-Kill (FOK)** — executes only if the entire quantity can be filled immediately

Both **successful and failing FOK scenarios** are explicitly handled.

---

## Core Design

- Price–time priority matching
- Deterministic execution
- Explicit separation of:
  - matching logic
  - event observation
  - correctness validation
- Safe order cancellation and modification
- No hidden state across runs

---

## Deterministic Correctness Validation (Golden vs Replay)

Correctness is validated using a **trace–replay approach**.

### Validation Flow

1. **Golden run**
   - Orders are processed normally
   - All state-changing actions (ADD / CANCEL / TRADE / MODIFY) are recorded as events

2. **Replay run**
   - Recorded events are replayed into a fresh engine instance

3. **Snapshot comparison**
   - Final order-book snapshots (aggregated by price level) are compared

Matching snapshots guarantee:
- deterministic behavior
- absence of hidden state
- correctness across all supported order types

Generated artifacts (events, traces, snapshots) are **local outputs only** and are **not committed**.

In addition to trace–replay validation, a lightweight assert-based unit test harness is provided to validate individual order type semantics in isolation.

Detailed benchmark methodology and performance measurements are documented in `bench/README.bench.md`.

---

## Memory Management
   Orders are stored using a fixed-capacity object pool (Orderpool).
   - Orders are preallocated at Orderbook construction.
   - AddOrder() acquires an Order instance from the pool.
   - Filled and cancelled orders are explicitly released.
   - Orders are reused via ResetOrder().

This replaces a previous std::shared_ptr-based design and removes reference-count overhead and dynamic allocation of Objects, while preserving deterministic behavior.

---

## Threaded Benchmark (Feed → SPSC → Engine)

In addition to the single-threaded benchmark harness, a multi-threaded integration benchmark is provided:

- Simulated market feed thread
- Lock-free SPSC queue
- Dedicated matching engine thread
- Core-pinned execution for reproducibility
- Controlled inter-arrival pacing (e.g., 60µs)
- Queue depth monitoring
- Engine utilization measurement
- End-to-end latency percentiles (p50 / p90 / p99 / p999)

The threaded benchmark operates in a feed-limited regime to isolate engine behavior from burst-induced saturation.

This benchmark is intended to validate:
- correctness under concurrent integration
- absence of message loss
- deterministic behavior under pacing
- basic queue backpressure characteristics

It does not attempt to model exchange-grade networking, kernel bypass, or microsecond-level jitter tuning.

### Example Feed-Limited Scenario

Configuration:

- 100,000 operations
- 60µs inter-arrival time (~16.6k msgs/sec)
- Lock-free SPSC queue (Feed → Engine)
- Core-pinned producer and engine threads
- Build flags: -O3 -march=native -DNDEBUG
- Hardware: Intel i5-4430 (x86_64, Linux)

Observed metrics:

- Throughput: ~16.6k ops/sec (matches feed rate)
- Average queue depth: ~1
- Max queue depth: 1
- Engine utilization: ~21.7%

Latency distribution:

- Queue latency:
  - p50: ~165 ns
  - p99: ~213 ns
- Engine processing latency:
  - p50: ~3.8 µs
  - p99: ~53.8 µs
- End-to-end latency (enqueue → processed):
  - p50: ~4.0 µs
  - p99: ~54.0 µs

This scenario operates in a feed-limited regime, where the matching engine processes messages faster than they are produced, resulting in minimal queue buildup and stable latency characteristics.

---

## Project Structure

```text
OME/
├── src/
│   ├── Orderbook.cpp
│   ├── benchmark_main.cpp
│   ├── orderbook_correctness.cpp
│   └── benchmark_threaded.cpp
├── include/
│   ├── Orderbook.h
│   ├── Order.h
│   ├── OrderType.h
│   ├── OrderModify.h
│   ├── LevelInfo.h
│   ├── OrderbookLevelInfos.h
│   ├── Side.h
│   ├── Usings.h
│   ├── TradeInfo.h
│   ├── Trade.h
│   ├── Event.h
│   ├── Benchmark.h
│   ├── Orderpool.h
│   ├── FeedMessage.h
│   └── SPSCQueue.h
├── bench/
│   ├── bench_config.h
│   └── README.bench.md
├── analysis/
│   ├── latency_analysis.py
│   └── latency_vs_size.png
├── Makefile
├── README.md
├── .clangd
└── .gitignore
```
---

## Build

### Linux
```bash
make correctness
make bench
make bench_threaded
```

### Windows (MinGW)

### Correctness tests
```bash
mingw32-make correctness
```
### Benchmark binary
```bash
mingw32-make bench
```
This builds the benchmark binary:
```
ome_benchmark.exe
```
---
## Running Correctness Validation

Run the benchmark in correctness mode with event logging enabled:
```
./ome_benchmark.exe --mode=correctness --events
```

This run:
- executes predefined correctness scenarios
- records golden and replay event streams
- produces final order-book snapshots
- validates deterministic behavior

## Running Perf Mode

Run the benchmark in perf mode:
```
./ome_benchmark.exe --mode=perf
```

## Running Threaded benchmark

Run the threaded benchmark:
```
./ome_benchmark_threaded.exe
```

## Performance Evolution

The engine initially used `std::shared_ptr` for order ownership.
This was later replaced with a fixed-capacity `Orderpool` to eliminate reference counting and dynamic allocation overhead.
A multi-threaded SPSC integration benchmark was later added to validate engine behavior under concurrent feed simulation.

Measured improvements (single-threaded, -O3 -march=native, Intel i5-4430, Linux x86_64):

### 100k-100k scenario (random_ops)

- Throughput: ~28% improvement
- p50 latency: 200ns → 114ns
- p99 latency: 899µs → 550µs (~39% reduction)

### 200k-200k scenario (random_ops)

- Throughput: ~31% improvement
- p50 latency: 300ns → 168ns
- p99 latency: 2.90ms → 1.63ms (~44% reduction)

These improvements are attributed to:
- Removal of atomic reference counting
- Elimination of per-order heap allocation
- Improved cache locality and memory reuse



## Notes
- This project focuses on deterministic matching semantics and controlled benchmarking rather than exchange-specific kernel bypass or networking optimizations.
- The benchmark harness supports both deterministic correctness validation and controlled performance analysis.
- Performance benchmarks are provided for baseline analysis and are not presented as exchange-grade latency claims.
