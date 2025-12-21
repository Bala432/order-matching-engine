#pragma once

#include "Usings.h"
#include "Side.h"
#include "Order.h"
#include "Trade.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Event.h"
#include <map>
#include <unordered_map>

class Orderbook{
private:
    struct OrderEntry{
        OrderPointer order_{ nullptr };
        OrderPointers::iterator location_;
    };

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    size_t matchedOrders_ = 0;
    Price bestBid_{0};
    Price bestAsk_{0};
    
    Side lastAggressorSide_{ Side::Buy };

    bool CanFullyFill(Side side, Price price, Quantity quantity) const;
    bool CanFullyFill_Buy(Price price, Quantity quantity) const;
    bool CanFullyFill_Sell(Price price, Quantity quantity) const;
    bool CanMatch(Side side, Price price) const;

    EventObserver observer_;
    void EmitEvent(const Event &e);
    uint64_t event_seq_{0}; 
    bool events_enabled_{false};

public:
    Orderbook();
    Orderbook(const Orderbook& ) = delete;
    void operator=(const Orderbook& ) = delete;
    Orderbook(Orderbook&&) = delete;
    void operator=(Orderbook&&) = delete;
    ~Orderbook();

    Trades AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    Trades MatchOrder(OrderModify order);
    Trades MatchOrders();

    // Size of Orderbook
    size_t Size() const;    
    size_t GetMatchedOrders() const;

    Price GetBestBidPrice() const;
    Price GetBestAskPrice() const;
    void UpdateBestPrices();

    OrderbookLevelInfos GetOrderInfos() const;

    // register an event observer
    void SetObserver(EventObserver obs);
    void EnableEvents(bool enabled);
};