#include "core/FileOperationQueue.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace usb_bridge {

FileOperationQueue::FileOperationQueue(const std::string& localBufferPath, uint64_t maxLocalBufferSize)
    : m_localBufferPath(localBufferPath)
    , m_maxLocalBufferSize(maxLocalBufferSize)
    , m_currentBufferUsage(0)
    , m_running(false)
    , m_paused(false)
    , m_nextId(1)
    , m_stats({0, 0, 0, 0, 0, 0, 0.0})
{
    // Create local buffer directory if it doesn't exist
    fs::create_directories(m_localBufferPath);
    
    // Calculate current buffer usage
    m_currentBufferUsage = calculateBufferUsage();
    
    Logger::info("FileOperationQueue initialized with buffer path: " + m_localBufferPath);
    Logger::info("Max buffer size: " + std::to_string(m_maxLocalBufferSize / (1024*1024)) + " MB");
    Logger::info("Current buffer usage: " + std::to_string(m_currentBufferUsage / (1024*1024)) + " MB");
}

FileOperationQueue::~FileOperationQueue() {
    stop();
}

void FileOperationQueue::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) return;
    
    m_running = true;
    m_paused = false;
    m_processingThread = std::thread(&FileOperationQueue::processQueue, this);
    
    Logger::info("FileOperationQueue started");
}

void FileOperationQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
    }
    
    m_condition.notify_all();
    
    if (m_processingThread.joinable()) {
        m_processingThread.join();
    }
    
    Logger::info("FileOperationQueue stopped");
}

void FileOperationQueue::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
    Logger::info("FileOperationQueue paused");
}

void FileOperationQueue::resume() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = false;
    }
    m_condition.notify_all();
    Logger::info("FileOperationQueue resumed");
}

bool FileOperationQueue::isRunning() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

uint64_t FileOperationQueue::queueRead(const std::string& clientId, 
                                       const std::string& drivePath,
                                       std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto op = std::make_shared<FileOperation>();
    op->id = nextOperationId();
    op->type = OperationType::READ;
    op->status = OperationStatus::QUEUED;
    op->clientId = clientId;
    op->sourcePath = drivePath;
    op->queuedTime = std::chrono::system_clock::now();
    op->completionCallback = callback;
    
    // Get file size
    try {
        op->fileSize = fs::file_size(drivePath);
        op->requiresDirectAccess = !hasBufferSpace(op->fileSize);
    } catch (const std::exception& e) {
        op->fileSize = 0;
        op->requiresDirectAccess = false;
        Logger::error("Failed to get file size for read: " + std::string(e.what()));
    }
    
    m_operations[op->id] = op;
    m_queue.push(op);
    m_stats.totalOperations++;
    
    Logger::info("Queued READ operation #" + std::to_string(op->id) + 
                 " for client " + clientId + ": " + drivePath);
    
    m_condition.notify_one();
    return op->id;
}

uint64_t FileOperationQueue::queueWrite(const std::string& clientId,
                                        const std::string& localFilePath,
                                        const std::string& driveDestPath,
                                        uint64_t fileSize,
                                        std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto op = std::make_shared<FileOperation>();
    op->id = nextOperationId();
    op->type = OperationType::WRITE;
    op->status = OperationStatus::QUEUED;
    op->clientId = clientId;
    op->localBufferPath = localFilePath;
    op->destPath = driveDestPath;
    op->fileSize = fileSize;
    op->queuedTime = std::chrono::system_clock::now();
    op->completionCallback = callback;
    
    // Check if we need direct access
    op->requiresDirectAccess = !hasBufferSpace(fileSize);
    
    if (op->requiresDirectAccess) {
        Logger::warn("Write operation #" + std::to_string(op->id) + 
                    " requires direct access (size: " + std::to_string(fileSize / (1024*1024)) + " MB)");
    }
    
    m_operations[op->id] = op;
    m_queue.push(op);
    m_stats.totalOperations++;
    
    Logger::info("Queued WRITE operation #" + std::to_string(op->id) + 
                 " for client " + clientId + ": " + driveDestPath);
    
    m_condition.notify_one();
    return op->id;
}

uint64_t FileOperationQueue::queueDelete(const std::string& clientId,
                                         const std::string& drivePath,
                                         std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto op = std::make_shared<FileOperation>();
    op->id = nextOperationId();
    op->type = OperationType::DELETE;
    op->status = OperationStatus::QUEUED;
    op->clientId = clientId;
    op->sourcePath = drivePath;
    op->fileSize = 0;
    op->requiresDirectAccess = false;
    op->queuedTime = std::chrono::system_clock::now();
    op->completionCallback = callback;
    
    m_operations[op->id] = op;
    m_queue.push(op);
    m_stats.totalOperations++;
    
    Logger::info("Queued DELETE operation #" + std::to_string(op->id) + 
                 " for client " + clientId + ": " + drivePath);
    
    m_condition.notify_one();
    return op->id;
}

uint64_t FileOperationQueue::queueMkdir(const std::string& clientId,
                                        const std::string& drivePath,
                                        std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto op = std::make_shared<FileOperation>();
    op->id = nextOperationId();
    op->type = OperationType::MKDIR;
    op->status = OperationStatus::QUEUED;
    op->clientId = clientId;
    op->destPath = drivePath;
    op->fileSize = 0;
    op->requiresDirectAccess = false;
    op->queuedTime = std::chrono::system_clock::now();
    op->completionCallback = callback;
    
    m_operations[op->id] = op;
    m_queue.push(op);
    m_stats.totalOperations++;
    
    Logger::info("Queued MKDIR operation #" + std::to_string(op->id) + 
                 " for client " + clientId + ": " + drivePath);
    
    m_condition.notify_one();
    return op->id;
}

uint64_t FileOperationQueue::queueMove(const std::string& clientId,
                                       const std::string& driveSourcePath,
                                       const std::string& driveDestPath,
                                       std::function<void(const FileOperation&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto op = std::make_shared<FileOperation>();
    op->id = nextOperationId();
    op->type = OperationType::MOVE;
    op->status = OperationStatus::QUEUED;
    op->clientId = clientId;
    op->sourcePath = driveSourcePath;
    op->destPath = driveDestPath;
    op->fileSize = 0;
    op->requiresDirectAccess = false;
    op->queuedTime = std::chrono::system_clock::now();
    op->completionCallback = callback;
    
    m_operations[op->id] = op;
    m_queue.push(op);
    m_stats.totalOperations++;
    
    Logger::info("Queued MOVE operation #" + std::to_string(op->id) + 
                 " for client " + clientId + ": " + driveSourcePath + " -> " + driveDestPath);
    
    m_condition.notify_one();
    return op->id;
}

void FileOperationQueue::processQueue() {
    Logger::info("FileOperationQueue processing thread started");
    
    while (m_running) {
        std::shared_ptr<FileOperation> op;
        
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] { 
                return !m_running || (!m_paused && !m_queue.empty()); 
            });
            
            if (!m_running) break;
            if (m_paused || m_queue.empty()) continue;
            
            op = m_queue.front();
            m_queue.pop();
            op->status = OperationStatus::IN_PROGRESS;
            op->startTime = std::chrono::system_clock::now();
        }
        
        // Execute operation outside of lock
        bool success = executeOperation(op);
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            op->endTime = std::chrono::system_clock::now();
            
            if (success) {
                op->status = OperationStatus::COMPLETED;
                m_stats.completedOperations++;
            } else {
                if (op->requiresDirectAccess) {
                    op->status = OperationStatus::DIRECT_ACCESS_REQUIRED;
                    m_stats.directAccessOperations++;
                } else {
                    op->status = OperationStatus::FAILED;
                    m_stats.failedOperations++;
                }
            }
            
            // Update average operation time
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                op->endTime - op->startTime).count();
            m_stats.averageOperationTime = 
                (m_stats.averageOperationTime * (m_stats.completedOperations - 1) + duration) / 
                m_stats.completedOperations;
        }
        
        // Call completion callback
        if (op->completionCallback) {
            try {
                op->completionCallback(*op);
            } catch (const std::exception& e) {
                Logger::error("Exception in completion callback: " + std::string(e.what()));
            }
        }
    }
    
    Logger::info("FileOperationQueue processing thread stopped");
}

bool FileOperationQueue::executeOperation(std::shared_ptr<FileOperation> op) {
    try {
        switch (op->type) {
            case OperationType::READ:
                return executeRead(op);
            case OperationType::WRITE:
                return executeWrite(op);
            case OperationType::DELETE:
                return executeDelete(op);
            case OperationType::MKDIR:
                return executeMkdir(op);
            case OperationType::MOVE:
                return executeMove(op);
            default:
                Logger::error("Unknown operation type");
                return false;
        }
    } catch (const std::exception& e) {
        op->errorMessage = e.what();
        Logger::error("Operation #" + std::to_string(op->id) + " failed: " + e.what());
        return false;
    }
}

bool FileOperationQueue::executeRead(std::shared_ptr<FileOperation> op) {
    if (op->requiresDirectAccess) {
        Logger::warn("Read operation #" + std::to_string(op->id) + " requires direct access");
        return false;
    }
    
    // Copy file from drive to local buffer
    std::string bufferPath = allocateLocalBuffer(op->clientId, op->fileSize);
    if (bufferPath.empty()) {
        op->requiresDirectAccess = true;
        return false;
    }
    
    // Copy file
    std::ifstream src(op->sourcePath, std::ios::binary);
    std::ofstream dst(bufferPath, std::ios::binary);
    
    if (!src || !dst) {
        releaseLocalBuffer(bufferPath);
        throw std::runtime_error("Failed to open files for reading");
    }
    
    const size_t bufferSize = 1024 * 1024; // 1MB buffer
    std::vector<char> buffer(bufferSize);
    
    while (src.read(buffer.data(), bufferSize) || src.gcount() > 0) {
        dst.write(buffer.data(), src.gcount());
        op->bytesProcessed += src.gcount();
    }
    
    op->localBufferPath = bufferPath;
    m_stats.bytesRead += op->fileSize;
    
    Logger::info("Read operation #" + std::to_string(op->id) + " completed successfully");
    return true;
}

bool FileOperationQueue::executeWrite(std::shared_ptr<FileOperation> op) {
    if (op->requiresDirectAccess) {
        Logger::warn("Write operation #" + std::to_string(op->id) + " requires direct access");
        return false;
    }
    
    // Copy from local buffer to drive
    std::ifstream src(op->localBufferPath, std::ios::binary);
    std::ofstream dst(op->destPath, std::ios::binary);
    
    if (!src || !dst) {
        throw std::runtime_error("Failed to open files for writing");
    }
    
    const size_t bufferSize = 1024 * 1024; // 1MB buffer
    std::vector<char> buffer(bufferSize);
    
    while (src.read(buffer.data(), bufferSize) || src.gcount() > 0) {
        dst.write(buffer.data(), src.gcount());
        op->bytesProcessed += src.gcount();
    }
    
    // Clean up local buffer after successful write
    releaseLocalBuffer(op->localBufferPath);
    
    m_stats.bytesWritten += op->fileSize;
    
    Logger::info("Write operation #" + std::to_string(op->id) + " completed successfully");
    return true;
}

bool FileOperationQueue::executeDelete(std::shared_ptr<FileOperation> op) {
    fs::remove_all(op->sourcePath);
    Logger::info("Delete operation #" + std::to_string(op->id) + " completed successfully");
    return true;
}

bool FileOperationQueue::executeMkdir(std::shared_ptr<FileOperation> op) {
    fs::create_directories(op->destPath);
    Logger::info("Mkdir operation #" + std::to_string(op->id) + " completed successfully");
    return true;
}

bool FileOperationQueue::executeMove(std::shared_ptr<FileOperation> op) {
    fs::rename(op->sourcePath, op->destPath);
    Logger::info("Move operation #" + std::to_string(op->id) + " completed successfully");
    return true;
}

std::string FileOperationQueue::allocateLocalBuffer(const std::string& clientId, uint64_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!hasBufferSpace(size)) {
        Logger::warn("Insufficient buffer space for allocation: " + std::to_string(size / (1024*1024)) + " MB");
        return "";
    }
    
    // Generate unique buffer filename
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = "buffer_" + clientId + "_" + std::to_string(timestamp) + ".tmp";
    std::string fullPath = m_localBufferPath + "/" + filename;
    
    m_currentBufferUsage += size;
    
    Logger::debug("Allocated buffer: " + fullPath + " (" + std::to_string(size / (1024*1024)) + " MB)");
    return fullPath;
}

void FileOperationQueue::releaseLocalBuffer(const std::string& bufferPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    try {
        uint64_t fileSize = fs::file_size(bufferPath);
        fs::remove(bufferPath);
        m_currentBufferUsage -= fileSize;
        
        Logger::debug("Released buffer: " + bufferPath + " (" + std::to_string(fileSize / (1024*1024)) + " MB)");
    } catch (const std::exception& e) {
        Logger::error("Failed to release buffer: " + std::string(e.what()));
    }
}

uint64_t FileOperationQueue::calculateBufferUsage() const {
    uint64_t totalSize = 0;
    
    try {
        for (const auto& entry : fs::directory_iterator(m_localBufferPath)) {
            if (entry.is_regular_file()) {
                totalSize += entry.file_size();
            }
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to calculate buffer usage: " + std::string(e.what()));
    }
    
    return totalSize;
}

uint64_t FileOperationQueue::getAvailableBufferSpace() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxLocalBufferSize > m_currentBufferUsage ? 
           m_maxLocalBufferSize - m_currentBufferUsage : 0;
}

uint64_t FileOperationQueue::getUsedBufferSpace() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentBufferUsage;
}

bool FileOperationQueue::hasBufferSpace(uint64_t requiredSize) const {
    return getAvailableBufferSpace() >= requiredSize;
}

bool FileOperationQueue::cancelOperation(uint64_t operationId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_operations.find(operationId);
    if (it == m_operations.end()) {
        return false;
    }
    
    if (it->second->status == OperationStatus::IN_PROGRESS) {
        Logger::warn("Cannot cancel operation #" + std::to_string(operationId) + " - already in progress");
        return false;
    }
    
    // Remove from queue if still queued
    // Note: This is inefficient but simple. For production, use a different data structure
    std::queue<std::shared_ptr<FileOperation>> newQueue;
    while (!m_queue.empty()) {
        auto op = m_queue.front();
        m_queue.pop();
        if (op->id != operationId) {
            newQueue.push(op);
        }
    }
    m_queue = newQueue;
    
    m_operations.erase(it);
    Logger::info("Cancelled operation #" + std::to_string(operationId));
    return true;
}

OperationStatus FileOperationQueue::getOperationStatus(uint64_t operationId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_operations.find(operationId);
    if (it == m_operations.end()) {
        throw std::runtime_error("Operation not found: " + std::to_string(operationId));
    }
    
    return it->second->status;
}

std::shared_ptr<FileOperation> FileOperationQueue::getOperation(uint64_t operationId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_operations.find(operationId);
    if (it == m_operations.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::vector<std::shared_ptr<FileOperation>> FileOperationQueue::getQueuedOperations() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<FileOperation>> result;
    for (const auto& pair : m_operations) {
        if (pair.second->status == OperationStatus::QUEUED) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<FileOperation>> FileOperationQueue::getClientOperations(
    const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<FileOperation>> result;
    for (const auto& pair : m_operations) {
        if (pair.second->clientId == clientId) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

void FileOperationQueue::cleanupCompletedOperations(std::chrono::seconds olderThan) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::system_clock::now();
    std::vector<uint64_t> toRemove;
    
    for (const auto& pair : m_operations) {
        if (pair.second->status == OperationStatus::COMPLETED || 
            pair.second->status == OperationStatus::FAILED) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - pair.second->endTime);
            
            if (age >= olderThan) {
                // Clean up any remaining buffer files
                if (!pair.second->localBufferPath.empty()) {
                    releaseLocalBuffer(pair.second->localBufferPath);
                }
                toRemove.push_back(pair.first);
            }
        }
    }
    
    for (uint64_t id : toRemove) {
        m_operations.erase(id);
    }
    
    if (!toRemove.empty()) {
        Logger::info("Cleaned up " + std::to_string(toRemove.size()) + " completed operations");
    }
}

FileOperationQueue::Statistics FileOperationQueue::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

uint64_t FileOperationQueue::nextOperationId() {
    return m_nextId++;
}

} // namespace usb_bridge