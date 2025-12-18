// Benchmark harness for OME project (deterministic trace + replay + snapshot compare)
// Writes trace_ops_<scenario>.csv for each scenario (seed recorded)
// Writes snapshot_golden_<scenario>.txt and snapshot_replay_<scenario>.txt and compares them
// Writes event logs: events_golden_<scenario>.csv and events_replay_<scenario>.csv

#include "benchmark.h"   
#include "Orderbook.h"
#include "Order.h"
#include "OrderModify.h"
#include "bench_config.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <random>
#include <iomanip>
#include <string>
#include <chrono>
#include <algorithm>
#include <sstream>

// ---------- small helpers ----------
using namespace std::chrono;

struct PhaseMetrics 
{
    std::string scenario;
    std::string phase;
    uint64_t ops = 0;
    uint64_t ns = 0;
    uint64_t cycles = 0;
    double avg_ns() const { return ops ? (double)ns / ops : 0.0; }
    double cycles_per_op() const { return ops ? (double)cycles / ops : 0.0; }
};

static void print_metrics_console(const PhaseMetrics &m) 
{
    std::cout << m.scenario << " | " << m.phase << ":\n";
    std::cout << "  ops: " << m.ops << "\n";
    std::cout << "  total: " << (m.ns / 1e6) << " ms (" << m.ns << " ns)\n";
    std::cout << "  avg/op: " << std::fixed << std::setprecision(2) << m.avg_ns() << " ns\n";
    std::cout << "  cycles/op: " << std::fixed << std::setprecision(2) << m.cycles_per_op() << "\n";
    std::cout << "  throughput: " << std::fixed << std::setprecision(2) << (m.ops / (m.ns / 1e9)) << " ops/s\n\n";
}

static void append_csv(std::ofstream &f, const PhaseMetrics &m) 
{
    f << "\"" << m.scenario << "\"," 
      << "\"" << m.phase << "\"," 
      << m.ops << "," 
      << m.ns << "," 
      << m.cycles << "," 
      << std::fixed << std::setprecision(2) << m.avg_ns() << "," 
      << std::fixed << std::setprecision(2) << m.cycles_per_op() << "\n";
}

// ---------- trace helpers ----------
static void trace_write_header(std::ofstream &trace, uint64_t seed, const std::string &scenario) {
    trace << "# seed=" << seed << ",scenario=" << scenario << "\n";
}

static void trace_write_add(std::ofstream &trace, uint32_t id, int orderType, int side, int price, int qty) {
    trace << "ADD," << id << "," << orderType << "," << side << "," << price << "," << qty << "\n";
}

static void trace_write_cancel(std::ofstream &trace, uint32_t id) {
    trace << "CANCEL," << id << "\n";
}

static void trace_write_match(std::ofstream &trace) {
    trace << "MATCH\n";
}

static void trace_write_modify(std::ofstream &trace, uint32_t id, int side, int price, int qty) {
    trace << "MODIFY," << id << "," << side << "," << price << "," << qty << "\n";
}

// ---------- snapshot helpers ----------
static void write_snapshot(const std::string &filename, const Orderbook &ob) 
{
    auto infos = ob.GetOrderInfos();
    std::ofstream f(filename);
    if (!f) return;
    f << "matchedOrders," << ob.GetMatchedOrders() << "\n";
    f << "book_size," << ob.Size() << "\n";
    f << "bids_levels\n";
    for (const auto &li : infos.GetBids()) {
        f << li.price_ << "," << li.quantity_ << "\n";
    }
    f << "asks_levels\n";
    for (const auto &li : infos.GetAsks()) {
        f << li.price_ << "," << li.quantity_ << "\n";
    }
    f.close();
}

// textual compare of snapshot files; returns true if identical
static bool compare_snapshots(const std::string &a, const std::string &b, std::string &diffOut) 
{
    std::ifstream fa(a), fb(b);
    if (!fa || !fb) {
        diffOut = "One of the snapshot files could not be opened.\n";
        return false;
    }
    std::string la, lb;
    size_t lineNo = 0;
    std::ostringstream diff;
    bool same = true;
    while (true) {
        bool ra = (bool)std::getline(fa, la);
        bool rb = (bool)std::getline(fb, lb);
        if (!ra && !rb) break;
        ++lineNo;
        if (!ra || !rb || la != lb) {
            same = false;
            diff << "Line " << lineNo << ":\n  GOLDEN: " << (ra ? la : "<EOL>") << "\n  REPLAY : " << (rb ? lb : "<EOL>") << "\n";
        }
    }
    diffOut = diff.str();
    return same;
}

// textual compare of event logs
static bool compare_event_logs(const std::string &a, const std::string &b, std::string &diffOut) 
{
    return compare_snapshots(a, b, diffOut);
}

// ---------- replay trace into fresh Orderbook (returns snapshot filename) ----------
// Modified to also write event log for replay (gated behind ENABLE_EVENT_LOGGING)
static void replay_trace_and_write_snapshot(const std::string &traceFile,
                                            const std::string &outSnapshotFile,
                                            const std::string &eventsReplayFile,
                                            bool enableEventLogging) 
{
    std::ifstream in(traceFile);
    if (!in) {
        std::cerr << "[REPLAY] Cannot open trace file for replay: " << traceFile << "\n";
        return;
    }

    Orderbook ob; // fresh instance

    // prepare replay event log using shared_ptr to keep stream alive safely
    std::shared_ptr<std::ofstream> eventsReplayPtr;
    if (enableEventLogging) {
        eventsReplayPtr = std::make_shared<std::ofstream>(eventsReplayFile);
        if (eventsReplayPtr && eventsReplayPtr->is_open()) {
            *eventsReplayPtr << "# columns=seq,type,order_id,order_id2,price,qty,side\n";
            ob.SetObserver([eventsReplayPtr](const Event &ev) {
                try {
                    (*eventsReplayPtr) << ev.to_csv() << "\n";
                } catch (const std::exception &ex) {
                    std::cerr << "[REPLAY] Observer write failed: " << ex.what() << "\n";
                } catch (...) {
                    std::cerr << "[REPLAY] Observer write failed: unknown exception\n";
                }
            });
        } else {
            std::cerr << "[REPLAY] Warning: could not open events replay file: " << eventsReplayFile << "\n";
        }
    }

    std::string line;
    uint64_t lineno = 0;
    uint64_t ops_executed = 0;
    const uint64_t PROGRESS_EVERY = 5000; // print progress periodically

    while (std::getline(in, line)) {
        ++lineno;
        if (line.empty() || line[0] == '#') continue;

        if ((lineno & 1023) == 0) { 
            std::cout << "[REPLAY] reading line " << lineno << " (ops " << ops_executed << ")\n";
        }

        std::istringstream iss(line);
        std::string op;
        try {
            if (!std::getline(iss, op, ',')) {
                std::cerr << "[REPLAY] malformed line " << lineno << ": '" << line << "'\n";
                continue;
            }
            if (op == "ADD") {
                std::string token;
                // expect: ADD,id,type,side,price,qty
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing id");
                uint32_t id = static_cast<uint32_t>(std::stoul(token));
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing type");
                int tval = std::stoi(token);
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing side");
                int sval = std::stoi(token);
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing price");
                int price = std::stoi(token);
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing qty");
                int qty = std::stoi(token);

                auto o = std::make_shared<Order>(static_cast<OrderType>(tval),id, static_cast<Side>(sval), price, qty);
                ob.AddOrder(o);
                ++ops_executed;
            }
            else if (op == "CANCEL") {
                std::string token;
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing id");
                uint32_t id = static_cast<uint32_t>(std::stoul(token));
                ob.CancelOrder(id);
                ++ops_executed;
            }
            else if (op == "MATCH") {
                ob.MatchOrders();
                ++ops_executed;
            }
            else if (op == "MODIFY") {
                std::string token;
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing id");
                uint32_t id = static_cast<uint32_t>(std::stoul(token));
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing side");
                int sval = std::stoi(token);
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing price");
                int price = std::stoi(token);
                if (!std::getline(iss, token, ',')) throw std::runtime_error("missing qty");
                int qty = std::stoi(token);

                OrderModify om(id, static_cast<Side>(sval), price, qty);
                ob.MatchOrder(om);
                ++ops_executed;
            }
            else {
                std::cerr << "[REPLAY] Unknown op '" << op << "' at line " << lineno << ": '" << line << "'\n";
            }
            if ((ops_executed % PROGRESS_EVERY) == 0 && ops_executed != 0) {
                std::cout << "[REPLAY] executed " << ops_executed << " ops (line " << lineno << ")\n";
            }
        } catch (const std::exception &ex) {
            std::cerr << "[REPLAY] Exception at line " << lineno << ": " << ex.what()
                      << "  line='" << line << "'\n";
        } catch (...) {
            std::cerr << "[REPLAY] Unknown exception at line " << lineno << "  line='" << line << "'\n";
        }
    } 

    std::cout << "[REPLAY] Finished reading trace; processed " << ops_executed << " ops (lines read " << lineno << ")\n";

    // Unregister observer before closing stream
    ob.SetObserver(nullptr);

    if (eventsReplayPtr && eventsReplayPtr->is_open()) {
        eventsReplayPtr->close();
    }

    // Write final snapshot
    write_snapshot(outSnapshotFile, ob);
    std::cout << "[REPLAY] Wrote replay snapshot to " << outSnapshotFile << "\n";
}

// ---------- small util to extract id from OrderPointer ----------
static uint32_t safe_get_order_id(const OrderPointer &p) 
{
    if (!p) return 0;
    return p->GetOrderId();
}

// ---------- main harness ----------
int main(int argc, char** argv) 
{
    BenchConfig cfg;

    for (int i = 1; i < argc; i++) 
    {
        std::string arg = argv[i];

        if (arg == "--mode=correctness")
            cfg.mode = RunMode::Correctness;
        else if (arg == "--mode=perf")
            cfg.mode = RunMode::Performance;
        else if (arg == "--events")
            cfg.enable_events = true;
        else if (arg.starts_with("--out="))
            cfg.paths.root = arg.substr(6);
    }

    // --- configuration ---
    struct Scenario { std::string name; uint64_t bulk; uint64_t rnd_ops; };

    const bool CORRECTNESS_ONLY = (cfg.mode == RunMode::Correctness);   // <<< CHANGE THIS WHEN NEEDED
    const bool ENABLE_EVENT_LOGGING = (cfg.enable_events && CORRECTNESS_ONLY); // set false to disable event logging for faster perf runs

    std::vector<Scenario> scenarios;
    double QUERY_FRACTION;
    double CANCEL_FRACTION;
    double MATCH_FRACTION;
    uint64_t WARMUP_ORDERS;
    bool KEEP_PTRS;
    uint64_t base_seed;

    if (CORRECTNESS_ONLY) {
        // ---------------------------
        // CORRECTNESS TESTING PROFILE
        // ---------------------------
        scenarios = {
            {"correct_small_1", 20, 50},
            {"correct_small_2", 30, 60},
            {"correct_small_3", 40, 80},
            {"correct_small_4", 30, 50},
            {"correct_small_5", 50, 100}
        };

        QUERY_FRACTION  = 0.35;    // higher query ratio to test logic
        CANCEL_FRACTION = 0.30;
        MATCH_FRACTION  = 0.10;

        WARMUP_ORDERS   = 10;
        KEEP_PTRS       = true;    // easier snapshots
        base_seed       = 4242424242ULL;

        std::cout << "[MODE] CORRECTNESS ONLY\n";
    }
    else {
        // ---------------------------
        // PERFORMANCE BENCHMARK PROFILE
        // ---------------------------
        scenarios = {
            { "100k-100k", 100'000, 100'000 },
            { "500k-200k", 500'000, 200'000 },
            { "1M-500k",   1'000'000, 500'000 }
        };

        QUERY_FRACTION  = 0.40;
        CANCEL_FRACTION = 0.25;
        MATCH_FRACTION  = 0.05;

        WARMUP_ORDERS   = 50'000;
        KEEP_PTRS       = false;   // avoids storing millions of shared_ptrs → prevents memory blowup
        base_seed       = 123456789ULL;

        std::cout << "[MODE] PERFORMANCE BENCHMARKS\n";
    }

    const std::string CSV_FILE = cfg.paths.results + "bench_results.csv";

    std::cout << "=== OME Benchmark Harness (with trace+replay) ===\n";
    SetHighPriority();

    std::ofstream csv(CSV_FILE);
    csv << "scenario,phase,ops,total_ns,total_cycles,avg_ns,cycles_per_op\n";

    for (const auto &sc : scenarios) {
        uint64_t seed = base_seed ^ std::hash<std::string>{}(sc.name);
        std::mt19937_64 rng(seed);

        // Prepare RNG and dists
        std::uniform_int_distribution<int> price_dist(1, 1000);
        std::uniform_int_distribution<int> qty_dist(1, 10);
        std::uniform_real_distribution<double> op_choice(0.0, 1.0);

        std::cout << "=== Running scenario: " << sc.name << " (bulk=" << sc.bulk << ", rnd=" << sc.rnd_ops << ") ===\n";

        // trace file for this scenario
        std::string traceFile = cfg.paths.traces + std::string("trace_ops_") + sc.name + ".csv";
        std::ofstream trace(traceFile);
        trace_write_header(trace, seed, sc.name);

        // Prepare event log for golden run (gated by ENABLE_EVENT_LOGGING)
        std::string eventsGoldenFile = cfg.paths.events_golden + std::string("events_golden_") + sc.name + ".csv";
        std::shared_ptr<std::ofstream> eventsGoldenPtr;
        if (ENABLE_EVENT_LOGGING) {
            eventsGoldenPtr = std::make_shared<std::ofstream>(eventsGoldenFile);
            if (eventsGoldenPtr && eventsGoldenPtr->is_open()) {
                *eventsGoldenPtr << "# columns=seq,type,order_id,order_id2,price,qty,side\n";
            } else {
                std::cerr << "[SCENARIO " << sc.name << "] Warning: could not open events golden file: " << eventsGoldenFile << "\n";
                eventsGoldenPtr.reset();
            }
        }

        Orderbook ob;
        ob.EnableEvents(cfg.enable_events);

        // register observer for golden run (writes to events_golden_<scenario>.csv) if enabled
        if (eventsGoldenPtr) {
            ob.SetObserver([eventsGoldenPtr](const Event &ev) {
                try {
                    (*eventsGoldenPtr) << ev.to_csv() << "\n";
                } catch (const std::exception &ex) {
                    // swallow/log — we cannot let observer exceptions crash the engine
                    std::cerr << "Observer write failed: " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "Observer write failed: unknown exception\n";
                }
            });
        }

        std::vector<OrderPointer> stored;
        stored.reserve(static_cast<size_t>(sc.bulk) + static_cast<size_t>(sc.rnd_ops / 4));

        // --- Warmup ---
        {
            Timer t;
            for (uint64_t i = 0; i < WARMUP_ORDERS; ++i) {
                uint32_t id = static_cast<uint32_t>(10 + i);
                Side s = (i & 1) ? Side::Buy : Side::Sell;
                int price = price_dist(rng);
                int qty = qty_dist(rng);
                auto o = std::make_shared<Order>(OrderType::GoodTillCancel, id, s, price, qty);
                ob.AddOrder(o);
                trace_write_add(trace, id, static_cast<int>(OrderType::GoodTillCancel), static_cast<int>(s), price, qty);
                if (KEEP_PTRS && (i & 63) == 0) stored.push_back(o);
            }
            PhaseMetrics m{sc.name, "warmup", WARMUP_ORDERS, t.nanoseconds(), t.cycles()};
            print_metrics_console(m); append_csv(csv, m);
            for (auto &p : stored) ob.CancelOrder(safe_get_order_id(p));
            stored.clear();
        }
        
        if (CORRECTNESS_ONLY) 
        {
            // --- Explicit FOK correctness cases ---
            ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 900001, Side::Sell, 100, 10));
            trace_write_add(trace, 900001, static_cast<int>(OrderType::GoodTillCancel),static_cast<int>(Side::Sell), 100, 10);

            ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 900002, Side::Sell, 101, 10));
            trace_write_add(trace, 900002, static_cast<int>(OrderType::GoodTillCancel),static_cast<int>(Side::Sell), 101, 10);

            // FOK should succeed
            ob.AddOrder(std::make_shared<Order>(OrderType::FillOrKill, 900010, Side::Buy, 101, 15));
            trace_write_add(trace, 900010, static_cast<int>(OrderType::FillOrKill),static_cast<int>(Side::Buy), 101, 15);

            // FOK should fail (insufficient liquidity)
            ob.AddOrder(std::make_shared<Order>(OrderType::FillOrKill, 900011, Side::Buy, 101, 30));
            trace_write_add(trace, 900011, static_cast<int>(OrderType::FillOrKill),static_cast<int>(Side::Buy), 101, 30);
        }

        // --- Bulk insert ---
        PhaseMetrics bulkM{sc.name, "bulk_insert"};
        {
            Timer t;
            for (uint64_t i = 0; i < sc.bulk; ++i) {
                uint32_t id = static_cast<uint32_t>(1'000'000 + i);
                Side s = (i & 1) ? Side::Buy : Side::Sell;
                int price = price_dist(rng);
                int qty = qty_dist(rng);
                auto o = std::make_shared<Order>(OrderType::GoodTillCancel, id, s, price, qty);
                ob.AddOrder(o);
                trace_write_add(trace, id, static_cast<int>(OrderType::GoodTillCancel), static_cast<int>(s), price, qty);
                if (KEEP_PTRS) stored.push_back(o);
            }
            bulkM.ops = sc.bulk; bulkM.ns = t.nanoseconds(); bulkM.cycles = t.cycles();
            print_metrics_console(bulkM); append_csv(csv, bulkM);
        }

        std::vector<uint32_t> live_ids;
        live_ids.reserve(stored.size());
        for (auto &p : stored) live_ids.push_back(safe_get_order_id(p));
        std::uniform_int_distribution<size_t> idx_dist(0, live_ids.empty() ? 0 : live_ids.size() - 1);

        // --- Randomized ops (with modify + match usage) ---
        PhaseMetrics rndM{sc.name, "random_ops"};
        {
            Timer t;
            uint64_t count_adds = 0, count_cancels = 0, count_queries = 0, count_matches = 0;
            uint64_t count_modifies = 0;
            uint32_t next_add_id = static_cast<uint32_t>(2'000'000);

            for (uint64_t op = 0; op < sc.rnd_ops; ++op) {
                double r = op_choice(rng);

                // Query best
                if (r < QUERY_FRACTION) {
                    if ((op & 1) == 0) { volatile auto b = ob.GetBestBidPrice(); (void)b; }
                    else              { volatile auto a = ob.GetBestAskPrice(); (void)a; }
                    ++count_queries; continue;
                }

                // Cancel
                if (r < QUERY_FRACTION + CANCEL_FRACTION) {
                    if (!live_ids.empty()) {
                        size_t idx = idx_dist(rng) % live_ids.size();
                        uint32_t id = live_ids[idx];
                        ob.CancelOrder(id);
                        trace_write_cancel(trace, id);
                        live_ids[idx] = live_ids.back(); 
                        live_ids.pop_back();
                        if (!live_ids.empty()) idx_dist = std::uniform_int_distribution<size_t>(0, live_ids.size() - 1);
                        ++count_cancels;
                    }
                    continue;
                }

                // Match explicit
                if (r < QUERY_FRACTION + CANCEL_FRACTION + MATCH_FRACTION) {
                    ob.MatchOrders();
                    trace_write_match(trace);
                    ++count_matches; 
                    continue;
                }

                // Add or Modify
                // Occasionally pick an existing order and modify it to test MatchOrder(OrderModify)
                if ((op % 43) == 0 && !live_ids.empty()) {
                    // modify existing
                    size_t idx = idx_dist(rng) % live_ids.size();
                    uint32_t id = live_ids[idx];
                    Side s = (op & 1) ? Side::Buy : Side::Sell;
                    int price = price_dist(rng);
                    int qty = qty_dist(rng);
                    OrderModify om(id, s, price, qty);
                    ob.MatchOrder(om);
                    trace_write_modify(trace, id, static_cast<int>(s), price, qty);
                    ++count_modifies;
                    continue;
                }

                // Add new order (including occasional FOK/IOC/Market)
                {
                    uint32_t id = next_add_id++;
                    Side s = (op & 1) ? Side::Buy : Side::Sell;
                    int price = price_dist(rng);
                    int qty = qty_dist(rng);

                    OrderType type = OrderType::GoodTillCancel;
                    
                    // Explicit, non-overlapping coverage(Spread order types deterministically without bias)
                    if ((op % 97) == 0) type = OrderType::Market;
                    else if ((op % 61) == 0) type = OrderType::ImmediateOrCancel;
                    else if ((op % 43) == 0) type = OrderType::FillOrKill;

                    auto o = std::make_shared<Order>(type, id, s, price, qty);
                    ob.AddOrder(o);
                    trace_write_add(trace, id, static_cast<int>(type), static_cast<int>(s), price, qty);

                    if (KEEP_PTRS) stored.push_back(o);
                    live_ids.push_back(safe_get_order_id(o));
                    idx_dist = std::uniform_int_distribution<size_t>(0, live_ids.size() - 1);
                    ++count_adds;
                }
            }

            rndM.ops = sc.rnd_ops; rndM.ns = t.nanoseconds(); rndM.cycles = t.cycles();
            print_metrics_console(rndM); append_csv(csv, rndM);

            std::cout << " breakdown: adds=" << count_adds << " cancels=" << count_cancels
                      << " queries=" << count_queries << " matches=" << count_matches
                      << " modifies=" << count_modifies << "\n\n";
        }

        // Best-bid stress test
        {
            const uint64_t QOPS = 200'000;
            Timer t;
            for (uint64_t i = 0; i < QOPS; ++i) {
                volatile auto p = ob.GetBestBidPrice(); (void)p;
            }
            PhaseMetrics qm{sc.name, "bestbid_stress", QOPS, t.nanoseconds(), t.cycles()};
            print_metrics_console(qm); append_csv(csv, qm);
        }

        // write golden snapshot
        std::string goldenSnapshot = cfg.paths.snapshots_golden + std::string("snapshot_golden_") + sc.name + ".txt";
        write_snapshot(goldenSnapshot, ob);

        // unregister observer before closing the stream=
        ob.SetObserver(nullptr);

        // close events golden file & trace
        if (eventsGoldenPtr && eventsGoldenPtr->is_open()) eventsGoldenPtr->close();
        trace.close();

        // replay trace and write replay snapshot & replay events
        std::string replaySnapshot = cfg.paths.snapshots_replay + std::string("snapshot_replay_") + sc.name + ".txt";
        std::string eventsReplayFile = cfg.paths.events_replay + std::string("events_replay_") + sc.name + ".csv";
        replay_trace_and_write_snapshot(traceFile, replaySnapshot, eventsReplayFile, ENABLE_EVENT_LOGGING);

        // compare snapshots
        std::string diff;
        bool ok = compare_snapshots(goldenSnapshot, replaySnapshot, diff);

        if (!ok) {
            std::cerr << "REPLAY MISMATCH for scenario " << sc.name << ":\n" << diff << "\n";
        } else {
            std::cout << "REPLAY OK for scenario " << sc.name << "\n";
        }

        // compare event logs (optional; prints diff if mismatch)
        std::string eventDiff;
        bool events_ok = true;
        if (ENABLE_EVENT_LOGGING) {
            events_ok = compare_event_logs(eventsGoldenFile, eventsReplayFile, eventDiff);
            if (!events_ok) {
                std::cerr << "EVENT LOG MISMATCH for scenario " << sc.name << ":\n" << eventDiff << "\n";
            } else {
                std::cout << "EVENT LOGS MATCH for scenario " << sc.name << "\n";
            }
        } else {
            std::cout << "[SCENARIO " << sc.name << "] event logging disabled, skipping event compare\n";
        }

        std::cout << "Scenario " << sc.name << " finished. Orderbook size: " << ob.Size() << "\n\n";
    }

    csv.close();
    std::cout << "All scenarios finished. CSV results written to bench_results.csv\n";
    std::cout << "Trace files: trace_ops_<scenario>.csv\n";
    std::cout << "Snapshots: snapshot_golden_<scenario>.txt and snapshot_replay_<scenario>.txt\n";
    std::cout << "Event logs: events_golden_<scenario>.csv and events_replay_<scenario>.csv\n";
    std::cout << "If REPLAY MISMATCH appears, investigate non-deterministic behavior or differences in modify/match semantics.\n";
    return 0;
}
