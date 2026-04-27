#include "camera_wrapper/frame_callback_manager.h"

#include <iostream>

namespace camera_wrapper {

FrameCallbackManager::CallbackId FrameCallbackManager::registerCallback(Callback cb) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    CallbackId id = nextId_++;
    callbacks_[id] = std::move(cb);
    return id;
}

void FrameCallbackManager::unregisterCallback(CallbackId id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    callbacks_.erase(id);
}

void FrameCallbackManager::notifyAll(const ImageFrame& frame) {
    // Hold a shared read lock so concurrent notifyAll() calls are permitted
    // while register/unregister/clear are excluded.  The subscriber map is
    // iterated directly without copying.
    //
    // If a subscriber calls unregisterCallback() from within its callback,
    // the write-lock acquisition will block until this call returns — no
    // deadlock, and the removal takes effect immediately afterwards.
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto& [id, cb] : callbacks_) {
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
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return callbacks_.size();
}

void FrameCallbackManager::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    callbacks_.clear();
}

} // namespace camera_wrapper
