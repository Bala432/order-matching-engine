#pragma once

#include "Usings.h"
#include <functional>
#include <string>
#include <cstdint>

struct Event {
    enum Type : uint8_t 
    { 
        EVT_ADD = 1, 
        EVT_CANCEL = 2, 
        EVT_TRADE = 3, 
        EVT_MODIFY = 4 
    } type;
    
    uint64_t seq = 0;
    OrderId order_id = 0;   // primary order id (for add/cancel/modify)
    OrderId order_id2 = 0;  // secondary id (for trades: other side)
    Price price = -1;      
    Quantity qty = 0;        
    uint8_t side = 255;      // 0 = sell, 1 = buy, 255 = NA

    // convert to CSV (seq,type,order_id,order_id2,price,qty,side)
    std::string to_csv() const;
};

using EventObserver = std::function<void(const Event&)>;