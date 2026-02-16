#pragma once

#include "OrderType.h"
#include "Usings.h"
#include "Side.h"
#include <cstdint>

struct FeedMessage{
    enum class Action {
        ADD,
        MODIFY,
        CANCEL,
        QUERY
    };
    Action action;
    OrderType orderType;
    OrderId orderId;
    Side side;
    Price price;
    Quantity quantity;
    uint64_t enqueue_ts;
};