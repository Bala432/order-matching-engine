#pragma once
#include <string>

enum class RunMode {
    Correctness,
    Performance
};

struct BenchPaths {
    std::string root = "bench";

    // event logs
    std::string events_golden   = "bench/events/golden/";
    std::string events_replay   = "bench/events/replay/";

    // snapshots
    std::string snapshots_golden = "bench/snapshots/golden/";
    std::string snapshots_replay = "bench/snapshots/replay/";

    // perf results
    std::string results = "bench/results/";

    // traces
    std::string traces = "bench/traces/";
};

struct BenchConfig {
    RunMode mode = RunMode::Correctness;
    bool enable_events = false;
    BenchPaths paths;
};
