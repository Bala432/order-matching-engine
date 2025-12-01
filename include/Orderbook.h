#pragma once

#include "Usings.h"
#include "Side.h"
#include "Order.h"
#include "Trade.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include<map>
#include<unordered_map>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<atomic>

class Orderbook{
    private:
        struct OrderEntry{
            OrderPointer order_{ nullptr };
            OrderPointers::iterator location_;
        };

        struct LevelData{
            Quantity quantity_{};
            Quantity count_{};

            enum class Action{
                Add,
                Cancel,
                Match
            };
        };

        std::unordered_map<Price, LevelData> data_;
        std::map<Price, OrderPointers, std::greater<Price>> bids_;
        std::map<Price, OrderPointers, std::less<Price>> asks_;
        std::unordered_map<OrderId, OrderEntry> orders_;

        std::thread ordersPruneThread_;
        mutable std::mutex ordersMutex_;
        std::condition_variable shutdownConditionVariable_;
        std::atomic<bool> shutdown_{ false };

        size_t matchedOrders_ = 0;

        void PruneGoodForDayOrders();
        void CancelOrders(OrderIds orderIds);
        void CancelOrderInternal(OrderId orderId);

        void OnOrderAdded(OrderPointer order);
        void OnOrderCancelled(OrderPointer order);
        void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
        void UpdatelevelData(Price price, Quantity quantity, LevelData::Action action);

        bool CanFullyFill(Side side, Price price, Quantity quantity) const;
        bool canMatch(Side side, Price price) const;
        Trades MatchOrders();

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
        size_t Size() const;    
        OrderbookLevelInfos GetOrderInfos() const;
        size_t getMatchedOrders() const;
};