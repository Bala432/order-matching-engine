#pragma once

#include "Usings.h"
#include "OrderType.h"
#include "Side.h"
#include "Order.h"
#include <memory>

class OrderModify{
private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;

public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity) :
        orderId_{ orderId }, side_{ side }, price_{ price }, quantity_{ quantity }
    {}

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Quantity GetQuantity() const { return quantity_; }
    
    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
    }
};