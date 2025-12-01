#include "Orderbook.h"
#include<numeric>
#include<ctime>
#include<chrono>
#include <optional>

Orderbook::Orderbook() : ordersPruneThread_{
    [this]{
        PruneGoodForDayOrders();
    }
}
{ }

Orderbook::~Orderbook(){
    shutdown_.store(true, std::memory_order_release);
    shutdownConditionVariable_.notify_one();
    ordersPruneThread_.join();
}

void Orderbook::OnOrderAdded(OrderPointer order){
    UpdatelevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderCancelled(OrderPointer order){
    UpdatelevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Cancel);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled){
    UpdatelevelData(price, quantity, isFullyFilled ? LevelData::Action::Cancel : LevelData::Action::Match);
}

void Orderbook::UpdatelevelData(Price price, Quantity quantity, LevelData::Action action){
    auto& data = data_[price];
    data.count_ += action == LevelData::Action::Add ? 1 : action == LevelData::Action::Cancel ? -1 : 0;
    if(action == LevelData::Action::Add)
        data.quantity_ += quantity;
    else
        data.quantity_ -= quantity; 
    if(data.count_ == 0)
        data_.erase(price);
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const 
{
    if(!canMatch(side, price))
        return false;

    std::optional<Price> threshold;
    if(side == Side::Buy){
        const auto& [price, _] = *asks_.begin();
        threshold = price;
    }
    else if(side == Side::Sell){
        const auto& [price, _] = *bids_.begin();
        threshold = price;
    }

    for(const auto& [levelprice, leveldata] : data_){
        if(threshold.has_value() && 
          ((side == Side::Buy && threshold.value() > levelprice) ||
          (side == Side::Sell && threshold.value() < levelprice)))
           continue;

        if((side == Side::Buy && price < levelprice) ||
           (side == Side::Sell && price > levelprice))
           continue;

        if(quantity <= leveldata.quantity_)
            return true;

        quantity -= leveldata.quantity_;
    }
    return false;
}

void Orderbook::PruneGoodForDayOrders(){
    using namespace std::chrono;
    const auto end = hours(16);

    while(true){
        const auto now = system_clock::now();
        const auto now_c = system_clock::to_time_t(now);
        std::tm now_parts;
        localtime_s(&now_parts, &now_c);

        if(now_parts.tm_hour >= end.count())
            now_parts.tm_mday += 1;

        now_parts.tm_hour = end.count();
        now_parts.tm_min = 0;
        now_parts.tm_sec = 0;

        auto next = system_clock::from_time_t(mktime(&now_parts));
        auto till = next - now + milliseconds(100);
        {
            std::unique_lock ordersLock{ ordersMutex_ };
            if(shutdown_.load(std::memory_order_acquire) ||
                shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
                return;
        }

        OrderIds orderIds;
        {
            std::unique_lock ordersLock{ ordersMutex_ };
            for(const auto& [_, entry] : orders_){
                const auto& [order, _] = entry;
                if(order->GetOrderType() == OrderType::GoodForDay)
                    orderIds.push_back(order->GetOrderID());
            }
        }
        CancelOrders(orderIds);
    }
}

void Orderbook::CancelOrders(OrderIds orderIds){
    std::lock_guard ordersLock{ ordersMutex_ };

    for(const auto& orderId : orderIds)
        CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
    if(!orders_.contains(orderId))
        return ;

    const auto& [order, iterator] = orders_.at(orderId);
    orders_.erase(orderId);

    if(order->GetSide() == Side::Buy){
        auto& bids = bids_[order->GetPrice()];
        bids.erase(iterator);
        if(bids.empty())
            bids_.erase(order->GetPrice());
    }
    if(order->GetSide() == Side::Sell){
        auto& asks = asks_[order->GetPrice()];
        asks.erase(iterator);
        if(asks.empty())
            asks_.erase(order->GetPrice());
    }
    OnOrderCancelled(order);
}

bool Orderbook::canMatch(Side side, Price price) const {
    if(side == Side::Buy){
        if(asks_.empty()) return false;
        const auto& [bestAsk, _] = *asks_.begin();
        return price >= bestAsk;
    }
    else if(side == Side::Sell){
        if(bids_.empty()) return false;
        const auto& [bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }
    return false;
}

Trades Orderbook::MatchOrders(){
    Trades trades;
    trades.reserve(orders_.size());

    while(true)
    {
        if(bids_.empty() || asks_.empty()) 
            break;

        auto& [bidPrice, bids] = *bids_.begin();
        auto& [askPrice, asks] = *asks_.begin();

        if(bidPrice < askPrice)
            break;

        while(bids.size() && asks.size())
        {
            auto& bid = bids.front();
            auto& ask = asks.front();

            Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
            bid->Fill(quantity);
            ask->Fill(quantity);
                
            if(bid->isFilled()){
                orders_.erase(bid->GetOrderID());
                bids.pop_front();
            }

            if(ask->isFilled()){
                orders_.erase(ask->GetOrderID());
                asks.pop_front();
            }

            if(bids.empty())
                bids_.erase(bidPrice);

            if(asks.empty())
                asks_.erase(askPrice);

            trades.push_back(Trade{
                            TradeInfo{bid->GetOrderID(), bid->GetPrice(), quantity},
                            TradeInfo{ask->GetOrderID(), ask->GetPrice(), quantity}});

            matchedOrders_++;

            OnOrderMatched(bid->GetPrice(), quantity, bid->isFilled());
            OnOrderMatched(ask->GetPrice(), quantity, ask->isFilled());
        }
    }
    if(!bids_.empty()){
        auto& [_, bids] = *bids_.begin();
        auto& order = bids.front();
        if(order->GetOrderType() == OrderType::FillAndKill)
            CancelOrder(order->GetOrderID());
    }

    if(!asks_.empty()){
        auto& [_, asks] = *asks_.begin();
        auto& order = asks.front();
        if(order->GetOrderType() == OrderType::FillAndKill)
            CancelOrder(order->GetOrderID());
    }
    return trades;
}

Trades Orderbook::AddOrder(OrderPointer order)
{
    if(orders_.contains(order->GetOrderID()))
        return {};

    if(order->GetOrderType() == OrderType::Market){
        if(order->GetSide() == Side::Buy && !asks_.empty()){
            const auto& [worstAsk, _] = *asks_.rbegin();
            order->ToGoodTillCancel(worstAsk);
        }
        else if(order->GetSide() == Side::Sell && !bids_.empty()){
            const auto& [worstBid, _] = *bids_.rbegin();
            order->ToGoodTillCancel(worstBid);
        }
        else
            return { };
    }

    if(order->GetOrderType() == OrderType::FillAndKill && !canMatch(order->GetSide(), order->GetPrice()))
        return {};

    if(order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
        return { };

    OrderPointers::iterator iterator;
    if(order->GetSide() == Side::Buy){
        auto& orders = bids_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::prev(orders.end());
    }
    else if(order->GetSide() == Side::Sell){
        auto& orders = asks_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::prev(orders.end());
    }
    orders_.insert({order->GetOrderID(), OrderEntry{order, iterator}});
    OnOrderAdded(order);
    return MatchOrders();
}

Trades Orderbook::MatchOrder(OrderModify order)
{
    if(!orders_.contains(order.GetOrderID()))
        return {};

    const auto& [existingOrder, _] = orders_.at(order.GetOrderID());
    CancelOrder(order.GetOrderID());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
}

void Orderbook::CancelOrder(OrderId orderId){
    std::lock_guard ordersLock{ ordersMutex_ };
    CancelOrderInternal(orderId);
}

std::size_t Orderbook::Size() const 
{
    return orders_.size();
}

std::size_t Orderbook::getMatchedOrders() const{
    return matchedOrders_;
}
OrderbookLevelInfos Orderbook::GetOrderInfos() const 
{
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size());
    askInfos.reserve(orders_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers& orders){
        return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
            [](std::size_t runningSum, const OrderPointer& order) {
                return runningSum + order->GetRemainingQuantity();
        })};
    };

    for(const auto& [price, orders] : bids_)
        bidInfos.push_back(CreateLevelInfos(price, orders));

    for(const auto& [price, orders] : asks_)
        askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderbookLevelInfos{bidInfos, askInfos};
}
