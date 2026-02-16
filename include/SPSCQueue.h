#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

template<typename T>
class SPSCQueue{
private:
    std::vector<T> buffer_;
    size_t capacity_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
public:
    SPSCQueue(size_t capacity) : buffer_(capacity), capacity_(capacity){}

    bool push(const T& value){
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity_;

        if(next_tail == head_.load(std::memory_order_acquire))
            return false;

        buffer_[current_tail] = value;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& value){
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t current_tail = tail_.load(std::memory_order_acquire);

        if(current_head == current_tail)
            return false;

        value = buffer_[current_head];
        size_t next_head = (current_head + 1) % capacity_;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool empty(){
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    size_t size(){
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t depth = (tail >= head) ? (tail - head) : (capacity_ - (head - tail));
        return depth;
    }
};