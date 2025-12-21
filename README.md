# Order Matching Engine (C++)

A deterministic **order matching engine** implemented in modern C++, supporting core exchange-style order types and validated using a **deterministic trace–replay correctness harness**.

This project focuses on **correct matching semantics, determinism, and clean system design**, rather than exchange-specific networking or kernel-bypass optimizations.

---

## Features

### Supported Order Types

- **Good-Till-Cancel (GTC)** — rests in the book until filled or canceled
- **Market** — executes immediately by sweeping available opposite-side liquidity
  (implemented internally via aggressive IOC conversion)
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

In addition to trace–replay validation, a lightweight assert-based unit test
harness is provided to validate individual order type semantics in isolation.

Detailed benchmark methodology and performance measurements are documented
in `bench/README.bench.md`.

---

## Project Structure

```text
OME/
├── src/
│   ├── Orderbook.cpp
│   ├── benchmark_main.cpp
│   ├── orderbook_correctness.cpp
│   └── main.cpp
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
│   └── Benchmark.h
├── bench/
│   ├── bench_config.h
│   └── README.bench.md
├── analysis/
│   ├── latency_analysis.py
│   └── latency_vs_size.png
├── Makefile
├── README.md
└── .gitignore
```
---

## Build (Windows / MinGW)

### Correctness tests
```
mingw32-make correctness
```
### Benchmark binary
```
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


## Notes
- This project intentionally avoids exchange-specific optimizations (kernel bypass, networking, lock-free I/O)
- The focus is on correct matching semantics and determinism
- The benchmark harness exists primarily to support correctness claims
- Performance benchmarks are provided for baseline analysis and are not presented as exchange-grade latency claims.
