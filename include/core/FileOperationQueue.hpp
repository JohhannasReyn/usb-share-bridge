#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>

namespace usb_bridge {

enum class OperationType {
    READ,
    WRITE,
    DELETE,
    MKDIR,
    MOVE
};

enum class OperationStatus {
    QUEUED,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    DIRECT_ACCESS_REQUIRED
};

struct FileOperation {
    uint64_t id;
    OperationType type;
    OperationStatus status;
    std::string clientId;
    std::string sourcePath;      // Source path on USB drive or local buffer
    std::string destPath;        // Destination path
    std::string localBufferPath; // Path to local buffer file (for writes)
    uint64_t fileSize;
    uint64_t bytesProcessed;
    std::chrono::system_clock::time_point queuedTime;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    std::string errorMessage;
    bool requiresDirectAccess;   // Flag for files too large to buffer
    
    // Callback for operation completion
    std::function<void(const FileOperation&)> completionCallback;
};

class FileOperationQueue {
public:
    FileOperationQueue(const std::string& localBufferPath, uint64_t maxLocalBufferSize);
    ~FileOperationQueue();

    // Queue operations
    uint64_t queueRead(const std::string& clientId, 
                      const std::string& drivePath,
                      std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t queueWrite(const std::string& clientId,
                       const std::string& localFilePath,
                       const std::string& driveDestPath,
                       uint64_t fileSize,
                       std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t queueDelete(const std::string& clientId,
                        const std::string& drivePath,
                        std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t queueMkdir(const std::string& clientId,
                       const std::string& drivePath,
                       std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t queueMove(const std::string& clientId,
                      const std::string& driveSourcePath,
                      const std::string& driveDestPath,
                      std::function<void(const FileOperation&)> callback = nullptr);

    // Queue management
    bool cancelOperation(uint64_t operationId);
    OperationStatus getOperationStatus(uint64_t operationId) const;
    std::shared_ptr<FileOperation> getOperation(uint64_t operationId) const;
    std::vector<std::shared_ptr<FileOperation>> getQueuedOperations() const;
    std::vector<std::shared_ptr<FileOperation>> getClientOperations(const std::string& clientId) const;
    
    // Buffer management
    uint64_t getAvailableBufferSpace() const;
    uint64_t getUsedBufferSpace() const;
    bool hasBufferSpace(uint64_t requiredSize) const;
    void cleanupCompletedOperations(std::chrono::seconds olderThan);
    
    // Processing control
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const;
    
    // Statistics
    struct Statistics {
        uint64_t totalOperations;
        uint64_t completedOperations;
        uint64_t failedOperations;
        uint64_t directAccessOperations;
        uint64_t bytesRead;
        uint64_t bytesWritten;
        double averageOperationTime;
    };
    Statistics getStatistics() const;

private:
    void processQueue();
    bool executeOperation(std::shared_ptr<FileOperation> op);
    bool executeRead(std::shared_ptr<FileOperation> op);
    bool executeWrite(std::shared_ptr<FileOperation> op);
    bool executeDelete(std::shared_ptr<FileOperation> op);
    bool executeMkdir(std::shared_ptr<FileOperation> op);
    bool executeMove(std::shared_ptr<FileOperation> op);
    
    std::string allocateLocalBuffer(const std::string& clientId, uint64_t size);
    void releaseLocalBuffer(const std::string& bufferPath);
    uint64_t calculateBufferUsage() const;
    
    uint64_t nextOperationId();
    
    std::string m_localBufferPath;
    uint64_t m_maxLocalBufferSize;
    uint64_t m_currentBufferUsage;
    
    std::queue<std::shared_ptr<FileOperation>> m_queue;
    std::unordered_map<uint64_t, std::shared_ptr<FileOperation>> m_operations;
    
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    
    bool m_running;
    bool m_paused;
    std::thread m_processingThread;
    
    uint64_t m_nextId;
    Statistics m_stats;
};

} // namespace usb_bridge