// orderbook_correctness.cpp
// -------------------------
// Lightweight unit tests for Orderbook correctness.
// Used to validate:
// - Market order sweep behavior
// - IOC / FOK semantics
// - Partial fills and empty-book behavior
//
// This file is NOT part of benchmark or production runs.
// It is used prior to deterministic replay and performance testing.


#include "Orderbook.h"
#include "Order.h"
#include <cassert>
#include <iostream>

static void test_market_buy_sweeps_asks();
static void test_market_sell_sweeps_bids();
static void test_market_empty_book();
static void test_ioc_unchanged();
static void test_fok_unchanged();

static uint32_t total_qty(const Trades& trades) {
    uint32_t q = 0;
    for (const auto& t : trades)
        q += t.GetBidTrade().quantity_;
    return q;
}

void test_market_sell_sweeps_bids() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 101, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 10));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Buy, 98, 20));

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::Market, 10, Side::Sell, /*ignored*/ 0, 18)
    );

    assert(trades.size() == 3);
    assert(trades[0].GetBidTrade().price_ == 101);
    assert(trades[1].GetBidTrade().price_ == 100);
    assert(trades[2].GetBidTrade().price_ == 98);

    assert(ob.Size() == 1); // remaining bid at 98
}

void test_market_buy_sweeps_asks() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 101, 20));

    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::Market, 10, Side::Buy, 0, 25));

    assert(trades.size() == 2);
    assert(trades[0].GetAskTrade().price_ == 100);
    assert(trades[1].GetAskTrade().price_ == 101);
    assert(total_qty(trades) == 25);

    // Remaining ask at 101 → 5
    assert(ob.Size() == 1);
}

void test_market_buy_partial_fill() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::Market, 10, Side::Buy, 0, 20)
    );

    assert(trades.size() == 1);
    assert(total_qty(trades) == 5);
    assert(ob.Size() == 0); // no resting OrderType::Market order
}

void test_market_buy_empty_book() {
    Orderbook ob;

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::Market, 1, Side::Buy, 0, 10)
    );

    assert(trades.empty());
    assert(ob.Size() == 0);
}

void test_market_sell_empty_book() {
    Orderbook ob;

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::Market, 2, Side::Sell, 0, 10)
    );

    assert(trades.empty());
    assert(ob.Size() == 0);
}

void test_ioc_buy_partial() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::ImmediateOrCancel, 2, Side::Buy, 100, 20)
    );

    assert(total_qty(trades) == 10);
    assert(ob.Size() == 0);
}

void test_ioc_buy_no_match() {
    Orderbook ob;

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::ImmediateOrCancel, 1, Side::Buy, 100, 10)
    );

    assert(trades.empty());
    assert(ob.Size() == 0);
}

void test_fok_buy_fail() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::FillOrKill, 2, Side::Buy, 100, 20)
    );

    assert(trades.empty());
    assert(ob.Size() == 1); // original ask untouched
}


void test_fok_buy_success() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 5));

    auto trades = ob.AddOrder(
        std::make_shared<Order>(OrderType::FillOrKill, 3, Side::Buy, 100, 15)
    );

    assert(total_qty(trades) == 15);
    assert(ob.Size() == 0);
}

void test_gtc_resting_after_market() {
    Orderbook ob;

    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 99, 10));
    ob.AddOrder(std::make_shared<Order>(OrderType::Market, 2, Side::Buy, 0, 10));

    // No asks → OrderType::Market Side::Buy cancels
    assert(ob.Size() == 1);
}

int main() {
    test_market_buy_sweeps_asks();
    test_market_sell_sweeps_bids();
    test_market_buy_partial_fill();
    test_market_buy_empty_book();
    test_market_sell_empty_book();
    test_ioc_buy_partial();
    test_ioc_buy_no_match();
    test_fok_buy_fail();
    test_fok_buy_success();
    test_gtc_resting_after_market();

    std::cout << "ALL ORDERBOOK CORRECTNESS TESTS PASSED\n";
    return 0;
}


