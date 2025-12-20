#pragma once

#include "Usings.h"
#include "OrderType.h"
#include "Side.h"
#include "Constants.h"
#include <sstream>
#include <memory>
#include <list>

class Order{
private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;

public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity) : 
        orderType_{ orderType }, orderId_{ orderId }, side_{ side }, price_{ price }, initialQuantity_{ quantity }, remainingQuantity_{ quantity }
    {}

    Order(OrderId orderId, Side side, Quantity quantity)
    : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity)
    {}

    OrderType GetOrderType() const { return orderType_; }
    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }
    Price GetPrice() const { return price_; }

    bool IsFilled() const { return GetRemainingQuantity() == 0; }
    void Fill(Quantity quantity){
        if(quantity > GetRemainingQuantity()){
            std::ostringstream oss;
            oss << "Order (" << GetOrderId() << ") cannot be filled more than remaining quantity" << std::endl;
            throw std::logic_error(oss.str());
        }
        remainingQuantity_ -= quantity;
    }

    void ToImmediateOrCancel(Price price){
        if(price <= 0){
            std::ostringstream oss;
            oss << "Order (" << GetOrderId() << ") must be a tradeable price" << std::endl;
            throw std::logic_error(oss.str());
        }
        price_ = price;
        orderType_ = OrderType::ImmediateOrCancel;
    }
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;