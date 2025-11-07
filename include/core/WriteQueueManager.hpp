#pragma once

#include "core/FileOperationQueue.hpp"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>

namespace usb_bridge {

enum class WritePriority {
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

struct WriteRequest {
    uint64_t id;
    std::string clientId;
    ClientType clientType;
    std::string localPath;
    std::string drivePath;
    uint64_t fileSize;
    WritePriority priority;
    std::chrono::system_clock::time_point submittedTime;
    std::chrono::system_clock::time_point scheduledTime;
    uint64_t operationId;  // FileOperationQueue operation ID once queued
    bool queued;
    std::function<void(const FileOperation&)> callback;
};

// Comparator for priority queue (higher priority first, then FIFO)
struct WriteRequestComparator {
    bool operator()(const std::shared_ptr<WriteRequest>& a, 
                   const std::shared_ptr<WriteRequest>& b) const {
        // Higher priority comes first
        if (a->priority != b->priority) {
            return static_cast<int>(a->priority) < static_cast<int>(b->priority);
        }
        // Same priority: earlier submission time comes first
        return a->submittedTime > b->submittedTime;
    }
};

/**
 * WriteQueueManager - Manages write operations with priority and batching
 * 
 * This component sits on top of FileOperationQueue and provides:
 * - Priority-based write scheduling
 * - Batch optimization for small files
 * - Write coalescing for efficiency
 * - Per-client write throttling
 */
class WriteQueueManager {
public:
    WriteQueueManager(FileOperationQueue& operationQueue);
    ~WriteQueueManager();

    // Submit write requests
    uint64_t submitWrite(const std::string& clientId,
                        ClientType clientType,
                        const std::string& localPath,
                        const std::string& drivePath,
                        uint64_t fileSize,
                        WritePriority priority = WritePriority::NORMAL,
                        std::function<void(const FileOperation&)> callback = nullptr);

    // Priority management
    bool updatePriority(uint64_t requestId, WritePriority newPriority);
    WritePriority getPriority(uint64_t requestId) const;

    // Request management
    bool cancelWrite(uint64_t requestId);
    std::shared_ptr<WriteRequest> getWriteRequest(uint64_t requestId) const;
    std::vector<std::shared_ptr<WriteRequest>> getPendingWrites() const;
    std::vector<std::shared_ptr<WriteRequest>> getClientWrites(const std::string& clientId) const;

    // Batch management
    void enableBatching(bool enable);
    void setBatchSize(size_t maxFiles);
    void setBatchTimeout(std::chrono::milliseconds timeout);
    void flushBatch();  // Force immediate processing of batched writes

    // Throttling
    void setClientWriteLimit(const std::string& clientId, size_t maxConcurrent);
    void removeClientWriteLimit(const std::string& clientId);
    size_t getClientActiveWrites(const std::string& clientId) const;

    // Control
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const;

    // Statistics
    struct Statistics {
        uint64_t totalSubmitted;
        uint64_t totalQueued;
        uint64_t totalCompleted;
        uint64_t totalFailed;
        uint64_t currentPending;
        uint64_t batchesCreated;
        uint64_t writesCoalesced;
        std::chrono::milliseconds averageQueueTime;
    };
    Statistics getStatistics() const;

private:
    void schedulerThread();
    void processPendingWrites();
    void queueWriteRequest(std::shared_ptr<WriteRequest> request);
    bool canQueueWrite(const std::string& clientId) const;
    void onOperationCompleted(uint64_t requestId, const FileOperation& op);
    uint64_t nextRequestId();

    FileOperationQueue& m_operationQueue;

    std::priority_queue<std::shared_ptr<WriteRequest>,
                       std::vector<std::shared_ptr<WriteRequest>>,
                       WriteRequestComparator> m_priorityQueue;

    std::unordered_map<uint64_t, std::shared_ptr<WriteRequest>> m_requests;
    std::unordered_map<std::string, size_t> m_clientActiveWrites;
    std::unordered_map<std::string, size_t> m_clientWriteLimits;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;

    bool m_running;
    bool m_paused;
    std::thread m_schedulerThread;

    // Batching configuration
    bool m_batchingEnabled;
    size_t m_batchMaxFiles;
    std::chrono::milliseconds m_batchTimeout;
    std::chrono::system_clock::time_point m_lastBatchTime;
    std::vector<std::shared_ptr<WriteRequest>> m_currentBatch;

    uint64_t m_nextRequestId;
    Statistics m_stats;
};

} // namespace usb_bridge