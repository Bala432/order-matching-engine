// tests/test_load.cpp
#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <vector>
#include <memory>
#include <iostream>

#include "Order.h"
#include "Orderbook.h"
#include "TestHelpers.h"

// Order types to test — update these names if your enum uses different identifiers.
static const std::vector<OrderType> kOrderTypesToTest = {
    OrderType::GoodTillCancel,
    OrderType::Market,
    OrderType::FillOrKill,
    OrderType::FillAndKill,
    OrderType::GoodForDay
};

// Change per-type count here for quick runs; increase for perf runs.
static constexpr size_t N_per_type = 1000;

TEST(LoadTest, InsertMultipleOrderTypes) {
    using namespace std::chrono;

    // create test orderbook (attempt to disable background threads if supported)
    Orderbook ob = MakeTestOrderbook();
    std::mt19937_64 rng(12345);

    for (OrderType t : kOrderTypesToTest) {
        std::uniform_int_distribution<int> price_dist(90, 110);
        std::uniform_int_distribution<int> qty_dist(1, 100);

        std::vector<std::shared_ptr<Order>> orders;
        orders.reserve(N_per_type);

        for (size_t i = 0; i < N_per_type; ++i) {
            Price p = (Price)price_dist(rng);
            Quantity q = (Quantity)qty_dist(rng);
            Side side = (i & 1) ? Side::Sell : Side::Buy;
            OrderId id = (OrderId)((uint64_t) ( (uint64_t)t * 1000000ull + i + 1 )); // unique per type/id
            orders.push_back(MakeOrder(t, id, side, p, q));
        }

        // Insert and time
        auto t0 = high_resolution_clock::now();
        for (size_t i = 0; i < orders.size(); ++i) {
            std::string err;
            if (!TryAddOrder(ob, orders[i], err)) {
                std::cerr << "AddOrder failed for OrderType=" << (int)t
                          << " at index=" << i << " id=" << (uint64_t)(orders[i]->GetOrderID())
                          << " err=\"" << err << "\"\n";
                FAIL() << "AddOrder threw for type=" << (int)t << " index=" << i << " err=" << err;
            }
        }
        auto t1 = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(t1 - t0).count();

        // Snapshot check
        std::string err;
        auto maybeInfos = TryGetInfos(ob, err);
        ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw after inserting type=" << (int)t << " err=" << err;

        size_t bids = maybeInfos->GetBids().size();
        size_t asks = maybeInfos->GetAsks().size();
        std::cout << "OrderType=" << (int)t << " inserted " << N_per_type << " orders in " << ms << " ms"
                  << ", matched orders : " << ob.getMatchedOrders() << " (bids=" << bids << " asks=" << asks << ")\n";

        // Basic sanity: either some bids/asks exist or FOK-like semantics removed them — at least no crash.
        // If you want stricter checks per-order-type (exact expectations), we can add them once we know semantics.
        SUCCEED();
    }
}
