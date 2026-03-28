#include "camera_wrapper/frame_callback_manager.h"

#include <iostream>

namespace camera_wrapper {

FrameCallbackManager::CallbackId FrameCallbackManager::registerCallback(Callback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    CallbackId id = nextId_++;
    callbacks_[id] = std::move(cb);
    return id;
}

void FrameCallbackManager::unregisterCallback(CallbackId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(id);
}

void FrameCallbackManager::notifyAll(const ImageFrame& frame) {
    // Snapshot the subscriber list under the lock, then release before calling.
    // This prevents deadlock if a subscriber calls unregisterCallback() from
    // within its own callback.
    std::map<CallbackId, Callback> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = callbacks_;
    }

    for (auto& [id, cb] : snapshot) {
        try {
            cb(frame);
        } catch (const std::exception& e) {
            std::cerr << "[FrameCallbackManager] subscriber " << id << " threw: " << e.what()
                      << '\n';
        } catch (...) {
            std::cerr << "[FrameCallbackManager] subscriber " << id << " threw unknown exception\n";
        }
    }
}

std::size_t FrameCallbackManager::subscriberCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return callbacks_.size();
}

void FrameCallbackManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.clear();
}

} // namespace camera_wrapper
