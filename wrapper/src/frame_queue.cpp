#include "camera_wrapper/frame_queue.h"

namespace camera_wrapper {

FrameQueue::FrameQueue(std::size_t capacity, OverflowPolicy policy)
    : capacity_(capacity)
    , policy_(policy) {}

FrameQueue::~FrameQueue() {
    stop();
}

// ------------------------------------------------------------------ //
//  Runtime configuration                                               //
// ------------------------------------------------------------------ //

void FrameQueue::setCapacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_ = capacity;
    // Trim excess frames (drop oldest first).
    while (queue_.size() > capacity_) {
        queue_.pop_front();
        ++dropCount_;
    }
}

void FrameQueue::setOverflowPolicy(OverflowPolicy policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_ = policy;
}

std::size_t FrameQueue::capacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_;
}

OverflowPolicy FrameQueue::overflowPolicy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return policy_;
}

// ------------------------------------------------------------------ //
//  Producer API                                                        //
// ------------------------------------------------------------------ //

void FrameQueue::push(const ImageFrame& frame) {
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (stopped_)
            return;

        ++totalPushed_;

        if (queue_.size() < capacity_) {
            // Fast path: there is room.
            queue_.push_back(frame); // shallow copy (Mat ref-count++)
        } else {
            // Queue is full – apply overflow policy.
            switch (policy_) {
                case OverflowPolicy::DropOldest:
                    queue_.pop_front();
                    ++dropCount_;
                    queue_.push_back(frame);
                    break;

                case OverflowPolicy::DropAll:
                    dropCount_ += static_cast<uint64_t>(queue_.size());
                    queue_.clear();
                    queue_.push_back(frame);
                    break;

                case OverflowPolicy::DropNewest:
                    // Reject the incoming frame.
                    ++dropCount_;
                    return; // cv_ not notified – nothing new was added.

                case OverflowPolicy::Block:
                    // Block until a slot is free.
                    cv_.wait(lock, [this] { return stopped_ || queue_.size() < capacity_; });
                    if (stopped_)
                        return;
                    queue_.push_back(frame);
                    break;
            }
        }
    }
    cv_.notify_one();
}

// ------------------------------------------------------------------ //
//  Consumer API                                                        //
// ------------------------------------------------------------------ //

std::optional<ImageFrame> FrameQueue::pop(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto hasFrame = [this] { return stopped_ || ! queue_.empty(); };

    if (timeoutMs < 0) {
        // Wait forever.
        cv_.wait(lock, hasFrame);
    } else if (timeoutMs == 0) {
        // Non-blocking.
        if (queue_.empty())
            return std::nullopt;
    } else {
        // Timed wait.
        if (! cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), hasFrame))
            return std::nullopt;
    }

    if (queue_.empty())
        return std::nullopt; // stopped_ was set

    ImageFrame frame = std::move(queue_.front());
    queue_.pop_front();
    ++totalPopped_;

    // Notify a blocked producer (Block policy).
    lock.unlock();
    cv_.notify_one();

    return frame;
}

std::size_t FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool FrameQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

void FrameQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    cv_.notify_all(); // wake blocked producers (Block policy)
}

void FrameQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

void FrameQueue::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = false;
}

// ------------------------------------------------------------------ //
//  Statistics                                                          //
// ------------------------------------------------------------------ //

FrameQueueStats FrameQueue::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {totalPushed_, totalPopped_, dropCount_};
}

void FrameQueue::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    totalPushed_ = 0;
    totalPopped_ = 0;
    dropCount_ = 0;
}

} // namespace camera_wrapper
