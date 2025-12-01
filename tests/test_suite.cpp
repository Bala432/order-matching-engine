#include <gtest/gtest.h>
#include "TestHelpers.h"

// Grouped small correctness tests (10-order scenarios). Uses TryGetInfos to avoid
// default-constructing OrderbookLevelInfos.

TEST(SanityTest, SingleAddBid) {
    Orderbook ob;
    std::string err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(1, Side::Buy, 100, 10), err)) << "AddOrder threw: " << err;

    auto maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw: " << err;
    const auto &infos = *maybeInfos;
    const auto &bids = infos.GetBids();
    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0].price_, (Price)100);
    EXPECT_EQ(bids[0].quantity_, (Quantity)10);
}

TEST(AddOrders, TenOrdersBasicChecks) {
    Orderbook ob;
    std::string err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(1, Side::Buy, 100, 10), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(2, Side::Sell, 105, 5), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(3, Side::Buy, 101, 2), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(4, Side::Sell, 106, 8), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(5, Side::Buy, 99, 7), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(6, Side::Sell, 104, 1), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(7, Side::Buy, 100, 3), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(8, Side::Sell, 105, 4), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(9, Side::Buy, 102, 6), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(10, Side::Sell, 103, 2), err)) << err;

    auto maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw: " << err;
    const auto &infos = *maybeInfos;
    const auto &bids = infos.GetBids();
    const auto &asks = infos.GetAsks();

    ASSERT_GT(bids.size(), 0u);
    EXPECT_EQ(bids[0].price_, (Price)102);

    ASSERT_GT(asks.size(), 0u);
    EXPECT_EQ(asks[0].price_, (Price)103);

    bool found100 = false;
    for (const auto &lvl : bids) {
        if (lvl.price_ == (Price)100) { found100 = true; EXPECT_EQ(lvl.quantity_, (Quantity)13); }
    }
    EXPECT_TRUE(found100);

    bool found105ask = false;
    for (const auto &lvl : asks) {
        if (lvl.price_ == (Price)105) { found105ask = true; EXPECT_EQ(lvl.quantity_, (Quantity)9); }
    }
    EXPECT_TRUE(found105ask);
}

TEST(MatchingBasic, FullAndPartialMatch) {
    Orderbook ob;
    std::string err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(1, Side::Sell, 105, 10), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(2, Side::Sell, 106, 5), err)) << err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(3, Side::Buy, 105, 10), err)) << err;

    auto maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw: " << err;
    const auto &asks = maybeInfos->GetAsks();
    ASSERT_GT(asks.size(), 0u);
    EXPECT_EQ(asks[0].price_, (Price)106);

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(4, Side::Buy, 107, 3), err)) << err;

    maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw (2): " << err;
    bool found106 = false;
    for (const auto &lvl : maybeInfos->GetAsks()) {
        if (lvl.price_ == (Price)106) { found106 = true; EXPECT_EQ(lvl.quantity_, (Quantity)2); }
    }
    EXPECT_TRUE(found106);
}

TEST(Cancellations, CancelExistingAndNonExisting) {
    Orderbook ob;
    std::string err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(1, Side::Buy, 100, 10), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(2, Side::Sell, 110, 5), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(3, Side::Buy, 101, 4), err)) << err;

    auto maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw: " << err;
    bool found110 = false;
    for (const auto &lvl : maybeInfos->GetAsks()) { if (lvl.price_ == (Price)110) { found110 = true; break; } }
    EXPECT_TRUE(found110);

    EXPECT_TRUE(TryCancelOrder(ob, (OrderId)2, err)) << err;

    maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw after cancel: " << err;
    for (const auto &lvl : maybeInfos->GetAsks()) { EXPECT_NE(lvl.price_, (Price)110); }

    // Cancel non-existing should be no-op (not throw)
    EXPECT_TRUE(TryCancelOrder(ob, (OrderId)9999, err)) << err;
}

TEST(DepthOrdering, BidsDescAsksAsc) {
    Orderbook ob;
    std::string err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(1, Side::Buy, 100, 1), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(2, Side::Buy, 102, 1), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(3, Side::Buy, 101, 1), err)) << err;

    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(4, Side::Sell, 105, 1), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(5, Side::Sell, 103, 1), err)) << err;
    EXPECT_TRUE(TryAddOrder(ob, MakeGTC(6, Side::Sell, 104, 1), err)) << err;

    auto maybeInfos = TryGetInfos(ob, err);
    ASSERT_TRUE(maybeInfos.has_value()) << "GetOrderInfos() threw: " << err;

    const auto &bids = maybeInfos->GetBids();
    const auto &asks = maybeInfos->GetAsks();

    ASSERT_GE(bids.size(), 3u);
    EXPECT_EQ(bids[0].price_, (Price)102);
    EXPECT_EQ(bids[1].price_, (Price)101);
    EXPECT_EQ(bids[2].price_, (Price)100);

    ASSERT_GE(asks.size(), 3u);
    EXPECT_EQ(asks[0].price_, (Price)103);
    EXPECT_EQ(asks[1].price_, (Price)104);
    EXPECT_EQ(asks[2].price_, (Price)105);
}
