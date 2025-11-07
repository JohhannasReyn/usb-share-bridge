#include "core/WriteQueueManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>

namespace usb_bridge {

WriteQueueManager::WriteQueueManager(FileOperationQueue& operationQueue)
    : m_operationQueue(operationQueue)
    , m_running(false)
    , m_paused(false)
    , m_batchingEnabled(false)
    , m_batchMaxFiles(10)
    , m_batchTimeout(std::chrono::seconds(5))
    , m_nextRequestId(1)
    , m_stats({0, 0, 0, 0, 0, 0, 0, std::chrono::milliseconds(0)})
{
    m_lastBatchTime = std::chrono::system_clock::now();
    Logger::info("WriteQueueManager initialized");
}

WriteQueueManager::~WriteQueueManager() {
    stop();
}

void WriteQueueManager::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        Logger::warn("WriteQueueManager already running");
        return;
    }
    
    m_running = true;
    m_paused = false;
    m_schedulerThread = std::thread(&WriteQueueManager::schedulerThread, this);
    
    Logger::info("WriteQueueManager started");
}

void WriteQueueManager::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }
    
    m_condition.notify_all();
    
    if (m_schedulerThread.joinable()) {
        m_schedulerThread.join();
    }
    
    Logger::info("WriteQueueManager stopped");
}

void WriteQueueManager::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
    Logger::info("WriteQueueManager paused");
}

void WriteQueueManager::resume() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = false;
    }
    m_condition.notify_all();
    Logger::info("WriteQueueManager resumed");
}

bool WriteQueueManager::isRunning() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

uint64_t WriteQueueManager::submitWrite(const std::string& clientId,
                                        ClientType clientType,
                                        const std::string& localPath,
                                        const std::string& drivePath,
                                        uint64_t fileSize,
                                        WritePriority priority,
                                        std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto request = std::make_shared<WriteRequest>();
    request->id = nextRequestId();
    request->clientId = clientId;
    request->clientType = clientType;
    request->localPath = localPath;
    request->drivePath = drivePath;
    request->fileSize = fileSize;
    request->priority = priority;
    request->submittedTime = std::chrono::system_clock::now();
    request->queued = false;
    request->operationId = 0;
    request->callback = callback;
    
    m_requests[request->id] = request;
    m_priorityQueue.push(request);
    m_stats.totalSubmitted++;
    m_stats.currentPending++;
    
    Logger::info("Write request #" + std::to_string(request->id) + " submitted for client " + 
                clientId + ": " + drivePath + " (priority: " + 
                std::to_string(static_cast<int>(priority)) + ")");
    
    m_condition.notify_one();
    return request->id;
}

bool WriteQueueManager::updatePriority(uint64_t requestId, WritePriority newPriority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_requests.find(requestId);
    if (it == m_requests.end() || it->second->queued) {
        return false;
    }
    
    it->second->priority = newPriority;
    
    // Rebuild priority queue with new priority
    std::vector<std::shared_ptr<WriteRequest>> temp;
    while (!m_priorityQueue.empty()) {
        temp.push_back(m_priorityQueue.top());
        m_priorityQueue.pop();
    }
    
    for (auto& req : temp) {
        m_priorityQueue.push(req);
    }
    
    Logger::info("Updated priority for request #" + std::to_string(requestId) + 
                 " to " + std::to_string(static_cast<int>(newPriority)));
    return true;
}

WritePriority WriteQueueManager::getPriority(uint64_t requestId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return WritePriority::NORMAL;
    }
    
    return it->second->priority;
}

bool WriteQueueManager::cancelWrite(uint64_t requestId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return false;
    }
    
    if (it->second->queued) {
        // Already queued in FileOperationQueue, try to cancel there
        bool cancelled = m_operationQueue.cancelOperation(it->second->operationId);
        if (cancelled) {
            m_stats.currentPending--;
        }
        return cancelled;
    }
    
    // Remove from our priority queue (rebuild without this request)
    std::vector<std::shared_ptr<WriteRequest>> temp;
    bool found = false;
    
    while (!m_priorityQueue.empty()) {
        auto req = m_priorityQueue.top();
        m_priorityQueue.pop();
        
        if (req->id != requestId) {
            temp.push_back(req);
        } else {
            found = true;
        }
    }
    
    for (auto& req : temp) {
        m_priorityQueue.push(req);
    }
    
    if (found) {
        m_requests.erase(it);
        m_stats.currentPending--;
        Logger::info("Cancelled write request #" + std::to_string(requestId));
        return true;
    }
    
    return false;
}

std::shared_ptr<WriteRequest> WriteQueueManager::getWriteRequest(uint64_t requestId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::vector<std::shared_ptr<WriteRequest>> WriteQueueManager::getPendingWrites() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<WriteRequest>> result;
    for (const auto& pair : m_requests) {
        if (!pair.second->queued) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<WriteRequest>> WriteQueueManager::getClientWrites(
    const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<WriteRequest>> result;
    for (const auto& pair : m_requests) {
        if (pair.second->clientId == clientId) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

void WriteQueueManager::enableBatching(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batchingEnabled = enable;
    Logger::info("Batching " + std::string(enable ? "enabled" : "disabled"));
}

void WriteQueueManager::setBatchSize(size_t maxFiles) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batchMaxFiles = maxFiles;
    Logger::info("Batch size set to " + std::to_string(maxFiles));
}

void WriteQueueManager::setBatchTimeout(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batchTimeout = timeout;
    Logger::info("Batch timeout set to " + std::to_string(timeout.count()) + " ms");
}

void WriteQueueManager::flushBatch() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_currentBatch.empty()) {
        return;
    }
    
    Logger::info("Flushing batch of " + std::to_string(m_currentBatch.size()) + " writes");
    
    for (auto& request : m_currentBatch) {
        queueWriteRequest(request);
    }
    
    m_currentBatch.clear();
    m_lastBatchTime = std::chrono::system_clock::now();
    m_stats.batchesCreated++;
}

void WriteQueueManager::setClientWriteLimit(const std::string& clientId, size_t maxConcurrent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clientWriteLimits[clientId] = maxConcurrent;
    Logger::info("Set write limit for client " + clientId + ": " + std::to_string(maxConcurrent));
}

void WriteQueueManager::removeClientWriteLimit(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clientWriteLimits.erase(clientId);
    Logger::info("Removed write limit for client " + clientId);
}

size_t WriteQueueManager::getClientActiveWrites(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_clientActiveWrites.find(clientId);
    if (it == m_clientActiveWrites.end()) {
        return 0;
    }
    
    return it->second;
}

WriteQueueManager::Statistics WriteQueueManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void WriteQueueManager::schedulerThread() {
    Logger::info("WriteQueueManager scheduler thread started");
    
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        m_condition.wait(lock, [this] {
            return !m_running || (!m_paused && !m_priorityQueue.empty());
        });
        
        if (!m_running) break;
        if (m_paused) continue;
        
        // Check batch timeout
        if (m_batchingEnabled && !m_currentBatch.empty()) {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastBatchTime);
            
            if (elapsed >= m_batchTimeout) {
                flushBatch();
            }
        }
        
        // Process pending writes
        processPendingWrites();
        
        lock.unlock();
        
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    Logger::info("WriteQueueManager scheduler thread stopped");
}

void WriteQueueManager::processPendingWrites() {
    // Process high priority writes first
    while (!m_priorityQueue.empty()) {
        auto request = m_priorityQueue.top();
        
        // Check if already queued
        if (request->queued) {
            m_priorityQueue.pop();
            continue;
        }
        
        // Check client throttling
        if (!canQueueWrite(request->clientId)) {
            break;
        }
        
        // Check batching
        if (m_batchingEnabled && request->priority != WritePriority::CRITICAL) {
            m_currentBatch.push_back(request);
            m_priorityQueue.pop();
            
            if (m_currentBatch.size() >= m_batchMaxFiles) {
                flushBatch();
            }
            
            continue;
        }
        
        // Queue immediately (critical priority or batching disabled)
        m_priorityQueue.pop();
        queueWriteRequest(request);
    }
}

void WriteQueueManager::queueWriteRequest(std::shared_ptr<WriteRequest> request) {
    request->scheduledTime = std::chrono::system_clock::now();
    
    // Create callback wrapper
    auto requestId = request->id;
    auto callback = [this, requestId](const FileOperation& op) {
        this->onOperationCompleted(requestId, op);
    };
    
    // Queue in FileOperationQueue
    uint64_t opId = m_operationQueue.queueWrite(
        request->clientId,
        request->localPath,
        request->drivePath,
        request->fileSize,
        callback
    );
    
    request->operationId = opId;
    request->queued = true;
    
    // Update client active writes
    m_clientActiveWrites[request->clientId]++;
    
    m_stats.totalQueued++;
    
    auto queueTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        request->scheduledTime - request->submittedTime);
    
    Logger::info("Queued write request #" + std::to_string(request->id) + 
                 " as operation #" + std::to_string(opId) +
                 " (queue time: " + std::to_string(queueTime.count()) + " ms)");
}

bool WriteQueueManager::canQueueWrite(const std::string& clientId) const {
    auto limitIt = m_clientWriteLimits.find(clientId);
    if (limitIt == m_clientWriteLimits.end()) {
        return true;  // No limit set
    }
    
    auto activeIt = m_clientActiveWrites.find(clientId);
    size_t activeCount = (activeIt != m_clientActiveWrites.end()) ? activeIt->second : 0;
    
    return activeCount < limitIt->second;
}

void WriteQueueManager::onOperationCompleted(uint64_t requestId, const FileOperation& op) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_requests.find(requestId);
    if (it == m_requests.end()) {
        return;
    }
    
    auto& request = it->second;
    
    // Update client active writes
    if (m_clientActiveWrites[request->clientId] > 0) {
        m_clientActiveWrites[request->clientId]--;
    }
    
    // Update statistics
    m_stats.currentPending--;
    
    if (op.status == OperationStatus::COMPLETED) {
        m_stats.totalCompleted++;
        Logger::info("Write request #" + std::to_string(requestId) + " completed successfully");
    } else {
        m_stats.totalFailed++;
        Logger::error("Write request #" + std::to_string(requestId) + " failed: " + op.errorMessage);
    }
    
    // Calculate average queue time
    auto totalQueueTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        request->scheduledTime - request->submittedTime);
    
    if (m_stats.totalCompleted > 0) {
        auto totalMs = m_stats.averageQueueTime.count() * (m_stats.totalCompleted - 1) + 
                      totalQueueTime.count();
        m_stats.averageQueueTime = std::chrono::milliseconds(totalMs / m_stats.totalCompleted);
    } else {
        m_stats.averageQueueTime = totalQueueTime;
    }
    
    // Call user callback
    if (request->callback) {
        try {
            request->callback(op);
        } catch (const std::exception& e) {
            Logger::error("Exception in write completion callback: " + std::string(e.what()));
        }
    }
    
    // Clean up request
    m_requests.erase(it);
}

uint64_t WriteQueueManager::nextRequestId() {
    return m_nextRequestId++;
}

} // namespace usb_bridge