#include "camera_wrapper/status_callback_manager.h"

#include <iostream>

namespace camera_wrapper {

StatusCallbackManager::CallbackId StatusCallbackManager::registerCallback(Callback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    CallbackId id = nextId_++;
    callbacks_[id] = std::move(cb);
    return id;
}

void StatusCallbackManager::unregisterCallback(CallbackId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(id);
}

void StatusCallbackManager::notifyAll(const StatusEvent& event) {
    std::map<CallbackId, Callback> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = callbacks_;
    }

    for (auto& [id, cb] : snapshot) {
        try {
            cb(event);
        } catch (const std::exception& e) {
            std::cerr << "[StatusCallbackManager] subscriber " << id << " threw: " << e.what()
                      << '\n';
        } catch (...) {
            std::cerr << "[StatusCallbackManager] subscriber " << id
                      << " threw unknown exception\n";
        }
    }
}

std::size_t StatusCallbackManager::subscriberCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return callbacks_.size();
}

void StatusCallbackManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.clear();
}

} // namespace camera_wrapper
