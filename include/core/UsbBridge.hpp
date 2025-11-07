#pragma once

#include "StorageManager.hpp"
#include "HostController.hpp"
#include "MutexLocker.hpp"
#include "FileChangeLogger.hpp"
#include "ConfigManager.hpp"
#include "FileOperationQueue.hpp"
#include "network/NetworkManager.hpp"
#include "network/SmbServer.hpp"
#include "network/HttpServer.hpp"
#include "gui/GuiManager.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <vector>

namespace usb_bridge {

struct SystemStatus {
    bool driveConnected;
    bool usbHost1Connected;
    bool usbHost2Connected;
    bool networkActive;
    bool smbServerRunning;
    bool httpServerRunning;
    AccessMode currentAccessMode;
    std::string accessHolder;
    uint64_t queuedOperations;
    uint64_t availableBufferSpace;
    uint64_t usedBufferSpace;
    uint64_t driveCapacity;
    uint64_t driveUsed;
    uint64_t driveFree;
    std::string driveMountPoint;
    std::string driveFilesystem;
};

class UsbBridge {
public:
    UsbBridge();
    ~UsbBridge();

    // Main lifecycle
    bool initialize();
    void start();
    void stop();
    void mainLoop();

    // System status
    SystemStatus getStatus() const;
    bool isRunning() const;

    // Configuration
    ConfigManager& getConfig();
    const ConfigManager& getConfig() const;

    // Component access (for integration)
    StorageManager& getStorageManager();
    HostController& getHostController();
    MutexLocker& getMutexLocker();
    FileChangeLogger& getFileLogger();
    FileOperationQueue& getOperationQueue();
    NetworkManager& getNetworkManager();
    SmbServer& getSmbServer();
    HttpServer& getHttpServer();
    GuiManager& getGuiManager();

    // Client file operations (queue-based with automatic buffer management)
    // These return an operation ID that can be used to track progress
    
    uint64_t clientReadFile(const std::string& clientId,
                           ClientType clientType,
                           const std::string& drivePath,
                           std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t clientWriteFile(const std::string& clientId,
                            ClientType clientType,
                            const std::string& localBufferPath,
                            const std::string& driveDestPath,
                            uint64_t fileSize,
                            std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t clientDeleteFile(const std::string& clientId,
                             ClientType clientType,
                             const std::string& drivePath,
                             std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t clientCreateDirectory(const std::string& clientId,
                                   ClientType clientType,
                                   const std::string& drivePath,
                                   std::function<void(const FileOperation&)> callback = nullptr);
    
    uint64_t clientMoveFile(const std::string& clientId,
                           ClientType clientType,
                           const std::string& driveSourcePath,
                           const std::string& driveDestPath,
                           std::function<void(const FileOperation&)> callback = nullptr);
    
    // Operation management
    bool cancelOperation(uint64_t operationId);
    OperationStatus getOperationStatus(uint64_t operationId) const;
    std::shared_ptr<FileOperation> getOperation(uint64_t operationId) const;
    std::vector<std::shared_ptr<FileOperation>> getQueuedOperations() const;
    std::vector<std::shared_ptr<FileOperation>> getClientOperations(const std::string& clientId) const;
    
    // Direct access management (for large files)
    bool requestDirectAccess(const std::string& clientId,
                            ClientType clientType,
                            uint64_t operationId,
                            std::chrono::seconds timeout = std::chrono::seconds(30));
    
    void releaseDirectAccess(const std::string& clientId);
    
    // System maintenance
    void cleanupOldOperations();
    void checkDriveHealth();
    void updateSystemStatus();
    
    // Event handlers
    void onDriveConnected(const std::string& mountPoint);
    void onDriveDisconnected();
    void onClientConnected(const std::string& clientId, ClientType type);
    void onClientDisconnected(const std::string& clientId, ClientType type);
    void onOperationCompleted(const FileOperation& operation);
    void onDirectAccessRequired(const FileOperation& operation);

private:
    void monitoringThread();
    void maintenanceThread();
    void handleFileSystemEvents();
    void switchToDirectAccessMode(const std::string& clientId, ClientType type);
    void switchToBoardManagedMode();
    bool isLargeFile(uint64_t fileSize) const;
    
    // Core components
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<StorageManager> m_storage;
    std::unique_ptr<HostController> m_hostController;
    std::unique_ptr<MutexLocker> m_mutexLocker;
    std::unique_ptr<FileChangeLogger> m_fileLogger;
    std::unique_ptr<FileOperationQueue> m_operationQueue;
    
    // Network components
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<SmbServer> m_smbServer;
    std::unique_ptr<HttpServer> m_httpServer;
    
    // GUI component
    std::unique_ptr<GuiManager> m_gui;
    
    // Threading
    std::atomic<bool> m_running;
    std::thread m_monitoringThread;
    std::thread m_maintenanceThread;
    
    // System state
    mutable std::mutex m_statusMutex;
    SystemStatus m_status;
    
    // Configuration parameters (loaded from config)
    std::string m_localBufferPath;
    uint64_t m_maxLocalBufferSize;
    uint64_t m_largeFileThreshold;
    std::chrono::seconds m_operationCleanupAge;
    std::chrono::seconds m_maintenanceInterval;
};

} // namespace usb_bridge