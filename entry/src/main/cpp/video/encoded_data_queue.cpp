#include "encoded_data_queue.h"
#include <chrono>

void EncodedDataQueue::Push(std::unique_ptr<Packet> pkt) {
    if (stopped_.load())
        return;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.push(std::move(pkt));
    }
    cond_.notify_one();
}

std::unique_ptr<EncodedDataQueue::Packet> EncodedDataQueue::Pop(int ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    cond_.wait_for(lk, std::chrono::milliseconds(ms), [this] { return !queue_.empty() || stopped_.load(); });
    if (stopped_.load() || queue_.empty())
        return nullptr;
    auto p = std::move(queue_.front());
    queue_.pop();
    return p;
}

void EncodedDataQueue::Stop() {
    stopped_.store(true);
    cond_.notify_all();
}

size_t EncodedDataQueue::Size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.size();
}