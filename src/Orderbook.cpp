#include "Orderbook.h"
#include <numeric>

Orderbook::Orderbook(){}

Orderbook::~Orderbook(){}

void Orderbook::EmitEvent(const Event &e) {
    if (observer_) observer_(e);
}

void Orderbook::SetObserver(EventObserver obs) 
{ 
    observer_ = std::move(obs); 
}

void Orderbook::EnableEvents(bool enabled)
{
    events_enabled_ = enabled;
}

std::string Event::to_csv() const {
    // format: seq,type,order_id,order_id2,price,qty,side
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%llu,%u,%u,%u,%lld,%llu,%u",
        (unsigned long long) seq,
        static_cast<unsigned>(type),
        order_id,
        order_id2,
        static_cast<long long>(price),
        (unsigned long long)qty,
        static_cast<unsigned>(side)); 
    return std::string(buf, (n>0) ? n : 0);
}

Price Orderbook::GetBestBidPrice() const 
{
    return bestBid_; 
}

Price Orderbook::GetBestAskPrice() const 
{ 
    return bestAsk_; 
}

void Orderbook::UpdateBestPrices() {
    Price bidPrice = bids_.empty() ? 0 : bids_.begin()->first;
    Price askPrice = asks_.empty() ? 0 : asks_.begin()->first;
    bestBid_ = bidPrice;
    bestAsk_ = askPrice;
}

bool Orderbook::CanFullyFill_Buy(Price price, Quantity quantity) const 
{   
    Quantity level_quantity = 0;
    for(auto it = asks_.begin(); it!= asks_.end() && it->first <= price; it++){
        const auto& orders_list = it->second;
        for(const auto& order : orders_list)
            level_quantity += order->GetRemainingQuantity();

        if(quantity <= level_quantity)
            return true;

        quantity -= level_quantity;
    }
    return false;
}

bool Orderbook::CanFullyFill_Sell(Price price, Quantity quantity) const 
{   
    Quantity level_quantity = 0;
    for(auto it = bids_.begin(); it!= bids_.end() && it->first >= price; it++){
        const auto& orders_list = it->second;
        for(const auto& order : orders_list)
            level_quantity += order->GetRemainingQuantity();

        if(quantity <= level_quantity)
            return true;

        quantity -= level_quantity;
    }
    return false;
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const 
{
    if(!CanMatch(side, price))
        return false;

    if(side == Side::Buy)
        return CanFullyFill_Buy(price, quantity);
    else    
        return CanFullyFill_Sell(price, quantity);
}

void Orderbook::CancelOrder(OrderId orderId)
{
    if(!orders_.contains(orderId))
        return ;
    
    const auto& [order, iterator] = orders_.at(orderId);
    orders_.erase(orderId);

    Price price = order->GetPrice();
    if(order->GetSide() == Side::Buy){
        auto& bids = bids_[price];
        bids.erase(iterator);
        if(bids.empty())
            bids_.erase(price);
    }
    if(order->GetSide() == Side::Sell){
        auto& asks = asks_[price];
        asks.erase(iterator);
        if(asks.empty())
            asks_.erase(price);
    }
    // <<<<<< EVENT: CANCEL
    if (events_enabled_) 
    {
        Event ev;
        ev.type = Event::EVT_CANCEL;
        // assign deterministic sequence
        ev.seq = event_seq_++;
        ev.order_id = orderId;          // the canceled order id
        ev.order_id2 = 0;
        ev.price = order->GetPrice();
        ev.qty = order->GetRemainingQuantity();  // optional: canceled quantity if tracked
        ev.side = (order->GetSide() == Side::Buy) ? 1 : 0;
        EmitEvent(ev);
    }
    UpdateBestPrices();
}

bool Orderbook::CanMatch(Side side, Price price) const {
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

    while(!bids_.empty() && !asks_.empty())
    {
        auto& [bidPrice, bids] = *bids_.begin();
        auto& [askPrice, asks] = *asks_.begin();

        if(bidPrice < askPrice)
            break;

        while(!bids.empty() && !asks.empty())
        {
            auto& bid = bids.front();
            auto& ask = asks.front();

            Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
            bid->Fill(quantity);
            ask->Fill(quantity);

            Price tradePrice = ask->GetPrice();
            trades.push_back(Trade{
                            TradeInfo{bid->GetOrderId(), tradePrice, quantity},
                            TradeInfo{ask->GetOrderId(), tradePrice, quantity}});

            matchedOrders_++;
            
            // ---- EVENT: TRADE ----
            if (events_enabled_) 
            {
                Event ev;
                ev.type = Event::EVT_TRADE;
                ev.seq  = event_seq_++;
                ev.order_id  = bid->GetOrderId();
                ev.order_id2 = ask->GetOrderId();
                ev.price = tradePrice;
                ev.qty   = quantity;
                ev.side  = 255; 
                EmitEvent(ev);
            }

             if(bid->IsFilled()){
                orders_.erase(bid->GetOrderId());
                bids.pop_front();
            }

            if(ask->IsFilled()){
                orders_.erase(ask->GetOrderId());
                asks.pop_front();
            }                
        }
        if(bids.empty())
            bids_.erase(bidPrice);

        if(asks.empty())
            asks_.erase(askPrice);
    }

    auto cleanup_side = [&](auto& book){
        std::vector<OrderId> to_cancel;

        for (const auto& [price, orders_list] : book) {
            for (const auto& order : orders_list) {
                if (order->GetOrderType() != OrderType::GoodTillCancel) {
                    to_cancel.push_back(order->GetOrderId());
                }
            }
        }

        for (OrderId id : to_cancel) {
            CancelOrder(id);
        }
    };

    cleanup_side(bids_);
    cleanup_side(asks_);

    UpdateBestPrices();

    return trades;
}

Trades Orderbook::AddOrder(OrderPointer order)
{
    if(orders_.contains(order->GetOrderId()))
        return {};

    bool isMarket = (order->GetOrderType() == OrderType::Market);

    // MARKET ORDER PATH — match only, never insert
    if (isMarket) {
        Price aggressive = (order->GetSide() == Side::Buy)
            ? std::numeric_limits<Price>::max()
            : std::numeric_limits<Price>::min();

        // Convert to IOC — ensures remainder auto-canceled in cleanup
        order->ToImmediateOrCancel(aggressive);
    }

    if (!isMarket) {
        if(order->GetOrderType() == OrderType::ImmediateOrCancel && !CanMatch(order->GetSide(), order->GetPrice()))
            return {};

        if(order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
            return {};
    }

    OrderPointers::iterator iterator;
    auto& orders = (order->GetSide() == Side::Buy) ? bids_[order->GetPrice()] : asks_[order->GetPrice()];
    orders.push_back(order);
    iterator = std::prev(orders.end());

    UpdateBestPrices();
    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

    // <<<<<< EVENT: ADD
    if (events_enabled_) 
    {
        Event ev;
        ev.type = Event::EVT_ADD;
        ev.seq = event_seq_++;
        ev.order_id = order->GetOrderId();
        ev.order_id2 = 0;
        ev.price = order->GetPrice();
        ev.qty = order->GetInitialQuantity();
        ev.side = (order->GetSide() == Side::Buy) ? 1 : 0;
        EmitEvent(ev);
    }

    return MatchOrders();
}

Trades Orderbook::MatchOrder(OrderModify order)
{
    if(!orders_.contains(order.GetOrderId()))
        return {};

    const auto& [existingOrder, _] = orders_.at(order.GetOrderId());

    // Emit MODIFY event before we cancel/reinsert so logs show the modification intent
    if (events_enabled_) 
    {
        Event ev;
        ev.type = Event::EVT_MODIFY;
        ev.seq = event_seq_++;
        ev.order_id = order.GetOrderId();
        ev.order_id2 = 0;
        ev.price = order.GetPrice();
        ev.qty = order.GetQuantity();
        ev.side = (order.GetSide() == Side::Buy) ? 1 : 0;
        EmitEvent(ev);
    }

    CancelOrder(order.GetOrderId());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
}

std::size_t Orderbook::Size() const 
{
    return orders_.size();
}

std::size_t Orderbook::GetMatchedOrders() const{
    return matchedOrders_;
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const 
{
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(bids_.size());
    askInfos.reserve(asks_.size());

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
