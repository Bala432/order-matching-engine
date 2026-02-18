#include "Orderbook.h"
#include "SPSCQueue.h"
#include "FeedMessage.h"
#include "OrderModify.h"
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <pthread.h>
#include <sched.h>
#include <immintrin.h>

struct BenchmarkConfig {
    size_t ops;
    uint64_t interval_ns;
};

std::vector<BenchmarkConfig> scenarios = {
    {1000000, 750000},  // latency mode (750µs pacing)
    {1000000, 0}        // throughput mode (no pacing)
};


using Clock = std::chrono::steady_clock;
using NS = std::chrono::nanoseconds;

inline uint64_t now_ns() {
    return std::chrono::duration_cast<NS>(Clock::now().time_since_epoch()).count();
}

struct LatencyStats {
    std::vector<uint64_t> queue_lat;
    std::vector<uint64_t> engine_lat;
    std::vector<uint64_t> e2e_lat;
};

static uint64_t percentile(std::vector<uint64_t>& v, double p) {
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

void pin_thread(std::thread& t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(),sizeof(cpu_set_t),&cpuset);
}

void run_benchmark(size_t scenario_ops, uint64_t scenario_interval_ns) {
    size_t OPS = scenario_ops;
    constexpr size_t QUEUE_SIZE = 1 << 16;

    if (scenario_interval_ns > 0)
        std::cout << "Mode: Latency (paced)\n";
    else
        std::cout << "Mode: Throughput (saturated)\n";

    Orderbook ob(OPS);
    SPSCQueue<FeedMessage> queue(QUEUE_SIZE);
    std::atomic<bool> done{false};
    std::atomic<uint64_t> processed{0};

    LatencyStats stats;
    stats.queue_lat.reserve(OPS);
    stats.engine_lat.reserve(OPS);
    stats.e2e_lat.reserve(OPS);

    size_t max_queue_depth{0};
    uint64_t total_queue_depth{0};
    uint64_t depth_samples{0};

    // ---------------- ENGINE THREAD ----------------
    std::thread engine([&] {
        FeedMessage msg;

        while (true) {
            if (queue.pop(msg)) {
                uint64_t dequeue_ts = now_ns();
                uint64_t qlat = dequeue_ts - msg.enqueue_ts;
                uint64_t engine_start = now_ns();

                switch (msg.action) {
                    case FeedMessage::Action::ADD:
                        ob.AddOrder(msg.orderType,
                                    msg.orderId,
                                    msg.side,
                                    msg.price,
                                    msg.quantity);
                        break;
                        
                    case FeedMessage::Action::CANCEL:
                        ob.CancelOrder(msg.orderId);
                        break;

                    case FeedMessage::Action::MODIFY: {
                        OrderModify om(msg.orderId,
                                       msg.side,
                                       msg.price,
                                       msg.quantity);
                        ob.MatchOrder(om);
                        break;
                    }

                    case FeedMessage::Action::QUERY: {
                        if (msg.side == Side::Buy)
                            volatile auto p = ob.GetBestBidPrice();
                        else
                            volatile auto p = ob.GetBestAskPrice();
                        break;
                    }
                }

                uint64_t engine_end = now_ns();

                stats.queue_lat.push_back(qlat);
                stats.engine_lat.push_back(engine_end - engine_start);
                stats.e2e_lat.push_back(engine_end - msg.enqueue_ts);

                processed.fetch_add(1, std::memory_order_relaxed);
            } 
            else if (done.load(std::memory_order_acquire) && queue.empty()) {
                break;
            }
        }
    });

    // ---------------- PRODUCER THREAD ----------------
    std::thread producer([&] {

        std::mt19937_64 rng(123456);
        std::normal_distribution<double> price_dist(500.0, 5.0);
        std::geometric_distribution<int> qty_dist(0.4);
        std::uniform_real_distribution<double> choice(0.0, 1.0);

        std::vector<uint32_t> live_orders;
        live_orders.reserve(OPS);

        uint32_t next_id = 1;
        size_t queue_depth{0};

        uint64_t interval_ns = scenario_interval_ns; // 60µs
        uint64_t next_send = now_ns();

        for (size_t i = 0; i < OPS; ++i) {
            FeedMessage msg;
            double r = choice(rng);

            // ---- Distribution ----

            // ---- Occasional Query Load (5%) ----
            if (choice(rng) < 0.05) {
                msg.action = FeedMessage::Action::QUERY;
                msg.side = (rng() & 1) ? Side::Buy : Side::Sell;
            }
            else if (r < 0.60) {
                // GTC
                msg.action = FeedMessage::Action::ADD;
                msg.orderType = OrderType::GoodTillCancel;
            }
            else if (r < 0.85) {
                // CANCEL
                if (!live_orders.empty()) {
                    msg.action = FeedMessage::Action::CANCEL;
                    size_t idx = rng() % live_orders.size();
                    msg.orderId = live_orders[idx];
                    live_orders[idx] = live_orders.back();
                    live_orders.pop_back();
                } else continue;
            }
            else if (r < 0.92) {
                // MODIFY
                if (!live_orders.empty()) {
                    msg.action = FeedMessage::Action::MODIFY;
                    size_t idx = rng() % live_orders.size();
                    msg.orderId = live_orders[idx];
                } else continue;
            }
            else if (r < 0.96) {
                // IOC
                msg.action = FeedMessage::Action::ADD;
                msg.orderType = OrderType::ImmediateOrCancel;
            }
            else if (r < 0.99) {
                // MARKET
                msg.action = FeedMessage::Action::ADD;
                msg.orderType = OrderType::Market;
            }
            else {
                // FOK
                msg.action = FeedMessage::Action::ADD;
                msg.orderType = OrderType::FillOrKill;
            }

            bool is_add = (msg.action == FeedMessage::Action::ADD);
            bool is_modify = (msg.action == FeedMessage::Action::MODIFY);
            if (is_add || is_modify) {
                if(is_add) msg.orderId = next_id++;

                msg.side = (rng() & 1) ? Side::Buy : Side::Sell;
                int price = std::max(1, std::min(1000, (int)price_dist(rng)));
                int qty = std::min(20, qty_dist(rng) + 1);
                msg.price = price;
                msg.quantity = qty;

                if (is_add && msg.orderType == OrderType::GoodTillCancel)
                    live_orders.push_back(msg.orderId);
            }

            msg.enqueue_ts = now_ns();
            while (!queue.push(msg)) {}
            queue_depth = queue.size();
            total_queue_depth += queue_depth;
            depth_samples++;
            max_queue_depth = std::max(max_queue_depth, queue_depth);
            if (interval_ns > 0) {
                next_send += interval_ns;
                uint64_t now = now_ns();

                if (now < next_send) {
                    while (now_ns() < next_send) {
                        _mm_pause();
                    }
                } else {
                    // drift correction
                    next_send = now;
                }
            }
        }
        done.store(true, std::memory_order_release);
    });

    pin_thread(engine, 2);
    pin_thread(producer, 3);

    auto start = Clock::now();

    producer.join();
    engine.join();

    auto end = Clock::now();
    uint64_t total_ns =
        std::chrono::duration_cast<NS>(end - start).count();

    std::cout << "\n=== Results ===\n";
    std::cout << "Total processed: " << processed.load() << "\n";
    std::cout << "Total time: " << total_ns / 1e6 << " ms\n";
    std::cout << "Throughput: "
              << (OPS / (total_ns / 1e9))
              << " ops/sec\n";

    double average_queue_depth = depth_samples ? (double)total_queue_depth / depth_samples : 0.0;
    std::cout << "Average Queue Depth : " << average_queue_depth << "\n";
    std::cout << "Max Queue Depth : " << max_queue_depth << "\n";

    uint64_t total_engine_busy_ns{0};
    for(auto& lat_ns : stats.engine_lat)
        total_engine_busy_ns += lat_ns;

    double engine_utilization = (double)total_engine_busy_ns / total_ns;
    std::cout << "Engine Utilization : " << engine_utilization * 100.00 << "\n";

    uint64_t p50_q = percentile(stats.queue_lat, 0.50);
    uint64_t p90_q = percentile(stats.queue_lat, 0.90);
    uint64_t p99_q = percentile(stats.queue_lat, 0.99);
    uint64_t p999_q = percentile(stats.queue_lat, 0.999);

    uint64_t p50_e = percentile(stats.engine_lat, 0.50);
    uint64_t p90_e = percentile(stats.engine_lat, 0.90);
    uint64_t p99_e = percentile(stats.engine_lat, 0.99);
    uint64_t p999_e = percentile(stats.engine_lat, 0.999);

    uint64_t p50_e2e = percentile(stats.e2e_lat, 0.50);
    uint64_t p90_e2e = percentile(stats.e2e_lat, 0.90);
    uint64_t p99_e2e = percentile(stats.e2e_lat, 0.99);
    uint64_t p999_e2e = percentile(stats.e2e_lat, 0.999);

    std::cout << "\nQueue Latency p50: " << p50_q << " ns\n";
    std::cout << "Queue Latency p90: " << p90_q << " ns\n";
    std::cout << "\nQueue Latency p99: " << p99_q << " ns\n";
    std::cout << "Queue Latency p999: " << p999_q << " ns\n";

    std::cout << "Engine Latency p50: " << p50_e << " ns\n";
    std::cout << "Engine Latency p90: " << p90_e << " ns\n";
    std::cout << "Engine Latency p99: " << p99_e << " ns\n";
    std::cout << "Engine Latency p999: " << p999_e << " ns\n";

    std::cout << "E2E Latency p50: " << p50_e2e << " ns\n";
    std::cout << "E2E Latency p90: " << p90_e2e << " ns\n";
    std::cout << "E2E Latency p99: " << p99_e2e << " ns\n";
    std::cout << "E2E Latency p999: " << p999_e2e << " ns\n";

    auto max_e2e = *std::max_element(stats.e2e_lat.begin(), stats.e2e_lat.end());
    std::cout << "E2E Max Latency: " << max_e2e << " ns\n";
    std::cout << "Final Orderbook Size: " << ob.Size() << "\n";

}

int main() {
    std::cout << "=== Threaded OME Benchmark (Realistic Feed) ===\n";
    for (const auto& cfg : scenarios) {
        std::cout << "\n--- Scenario Operations : " << cfg.ops << " ---\n";
        run_benchmark(cfg.ops, cfg.interval_ns);
    }

    return 0;
}
