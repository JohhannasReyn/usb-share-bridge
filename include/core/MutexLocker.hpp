#pragma once

#include <mutex>

class MutexLocker {
public:
    explicit MutexLocker(std::mutex& mutex) : m_lock(mutex) {}
    
    // Non-copyable, movable
    MutexLocker(const MutexLocker&) = delete;
    MutexLocker& operator=(const MutexLocker&) = delete;
    MutexLocker(MutexLocker&&) = default;
    MutexLocker& operator=(MutexLocker&&) = default;

private:
    std::lock_guard<std::mutex> m_lock;
};