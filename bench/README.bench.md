# Benchmark Harness: Deterministic Correctness Validation

This folder documents the **correctness validation approach** used by the
Order Matching Engine.

The benchmark harness is designed to verify **deterministic behavior**
using a **trace–replay model**.

---

## Validation Concept

Correctness is validated in three logical steps:

1. **Golden execution**
   - Orders are processed normally by the engine
   - All state-changing actions (ADD / CANCEL / TRADE / MODIFY)
     are recorded as structured events with a deterministic sequence number

2. **Replay execution**
   - The recorded events are replayed into a fresh engine instance

3. **Snapshot comparison**
   - Final order-book snapshots (aggregated by price level)
     from golden and replay runs are compared

Matching snapshots guarantee:
- deterministic execution
- absence of hidden state
- correctness across supported order types

---

## Events and Snapshots

- **Event logs** capture every state transition in execution order
- **Snapshots** represent the final order-book state at price level granularity
- A monotonic sequence number (`seq`) is used instead of timestamps to ensure
  reproducibility

Generated artifacts (events, traces, snapshots) are produced **locally**
during correctness runs and are intentionally **not committed** to the repository.

---

## Scope

This benchmark harness focuses exclusively on **correctness validation**.
It is not intended to measure or compare performance characteristics.

Performance benchmarking, if performed, is handled separately and without
event logging.

---

## Notes

- The harness is implemented in `benchmark_main.cpp`
- Golden and replay runs are executed internally by the benchmark binary
- This design avoids external scripts and platform-specific tooling