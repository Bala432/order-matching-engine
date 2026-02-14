#pragma once

#include "Order.h"
#include <vector>

class Orderpool{
private:
    std::vector<Order> orders_;
    std::vector<size_t> free_list_;
public:
    Orderpool(size_t size) : orders_(size){
        free_list_.reserve(size);
        for(size_t index = size; index > 0; index--)
            free_list_.push_back(index - 1);
    }

    Order* acquire(){
        if(free_list_.empty()) return nullptr;

        size_t index = free_list_.back();
        free_list_.pop_back();
        return &orders_[index];
    }

    void release(Order* order){
        size_t index = order - &orders_[0];
        free_list_.push_back(index);
    }
};