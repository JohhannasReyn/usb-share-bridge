#include <chrono>
#include <thread>
#include "core/UsbBridge.hpp"
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace usb_bridge {

UsbBridge::UsbBridge()
    : m_running(false)
    , m_localBufferPath("/data/buffer")
    , m_maxLocalBufferSize(10ULL * 1024 * 1024 * 1024) // 10GB default
    , m_largeFileThreshold(5ULL * 1024 * 1024 * 1024)  // 5GB default
    , m_operationCleanupAge(std::chrono::hours(24))
    , m_maintenanceInterval(std::chrono::minutes(5))
{
    Logger::info("UsbBridge constructor");
}

UsbBridge::~UsbBridge() {
    stop();
}

bool UsbBridge::initialize() {
    Logger::info("Initializing USB Bridge system...");
    
    try {
        // Initialize configuration
        m_config = std::make_unique<ConfigManager>();
        if (!m_config->load("/etc/usb-bridge/system.json")) {
            Logger::warn("Failed to load system config, using defaults");
        }
        
        // Load buffer configuration
        if (m_config->hasKey("buffer.path")) {
            m_localBufferPath = m_config->getString("buffer.path");
        }
        if (m_config->hasKey("buffer.maxSize")) {
            m_maxLocalBufferSize = m_config->getUInt64("buffer.maxSize");
        }
        if (m_config->hasKey("buffer.largeFileThreshold")) {
            m_largeFileThreshold = m_config->getUInt64("buffer.largeFileThreshold");
        }
        
        Logger::info("Buffer configuration:");
        Logger::info("  Path: " + m_localBufferPath);
        Logger::info("  Max size: " + std::to_string(m_maxLocalBufferSize / (1024*1024)) + " MB");
        Logger::info("  Large file threshold: " + std::to_string(m_largeFileThreshold / (1024*1024)) + " MB");
        
        // Initialize core components
        m_mutexLocker = std::make_unique<MutexLocker>();
        m_fileLogger = std::make_unique<FileChangeLogger>("/data/logs");
        m_operationQueue = std::make_unique<FileOperationQueue>(m_localBufferPath, m_maxLocalBufferSize);
        m_storage = std::make_unique<StorageManager>("/mnt/usbdrive");
        m_hostController = std::make_unique<HostController>();
        
        // Initialize network components
        m_network = std::make_unique<NetworkManager>();
        m_smbServer = std::make_unique<SmbServer>();
        m_httpServer = std::make_unique<HttpServer>(8080);
        
        // Initialize GUI
        m_gui = std::make_unique<GuiManager>();
        
        Logger::info("USB Bridge system initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to initialize USB Bridge: " + std::string(e.what()));
        return false;
    }
}

void UsbBridge::start() {
    if (m_running) {
        Logger::warn("USB Bridge is already running");
        return;
    }
    
    Logger::info("Starting USB Bridge system...");
    
    m_running = true;
    
    // Start file operation queue
    m_operationQueue->start();
    
    // Start monitoring thread
    m_monitoringThread = std::thread(&UsbBridge::monitoringThread, this);
    
    // Start maintenance thread
    m_maintenanceThread = std::thread(&UsbBridge::maintenanceThread, this);
    
    // Start network services
    if (m_config->getBool("network.smb.enabled", true)) {
        m_smbServer->start();
    }
    
    if (m_config->getBool("network.http.enabled", true)) {
        m_httpServer->start();
    }
    
    // Start GUI
    m_gui->start();
    
    Logger::info("USB Bridge system started successfully");
}

void UsbBridge::stop() {
    if (!m_running) {
        return;
    }
    
    Logger::info("Stopping USB Bridge system...");
    
    m_running = false;
    
    // Stop operation queue
    if (m_operationQueue) {
        m_operationQueue->stop();
    }
    
    // Stop threads
    if (m_monitoringThread.joinable()) {
        m_monitoringThread.join();
    }
    
    if (m_maintenanceThread.joinable()) {
        m_maintenanceThread.join();
    }
    
    // Stop network services
    if (m_smbServer) {
        m_smbServer->stop();
    }
    
    if (m_httpServer) {
        m_httpServer->stop();
    }
    
    // Stop GUI
    if (m_gui) {
        m_gui->stop();
    }
    
    Logger::info("USB Bridge system stopped");
}

void UsbBridge::mainLoop() {
    while (m_running) {
        // Main event loop
        handleFileSystemEvents();
        updateSystemStatus();
        
        // Let GUI process events
        if (m_gui) {
            m_gui->update();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void UsbBridge::monitoringThread() {
    Logger::info("Monitoring thread started");
    
    while (m_running) {
        try {
            // Monitor storage
            if (m_storage) {
                m_storage->checkDriveStatus();
            }
            
            // Monitor hosts
            if (m_hostController) {
                m_hostController->checkHostConnections();
            }
            
            // Check for expired access grants
            if (m_mutexLocker && !m_mutexLocker->isBoardManaged()) {
                // Could check for timeouts here
            }
            
        } catch (const std::exception& e) {
            Logger::error("Error in monitoring thread: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    Logger::info("Monitoring thread stopped");
}

void UsbBridge::maintenanceThread() {
    Logger::info("Maintenance thread started");
    
    while (m_running) {
        try {
            // Cleanup old operations
            cleanupOldOperations();
            
            // Check drive health
            checkDriveHealth();
            
            // Update statistics
            updateSystemStatus();
            
        } catch (const std::exception& e) {
            Logger::error("Error in maintenance thread: " + std::string(e.what()));
        }
        
        std::this_thread::sleep_for(m_maintenanceInterval);
    }
    
    Logger::info("Maintenance thread stopped");
}

void UsbBridge::handleFileSystemEvents() {
    // Handle any file system events (inotify, etc.)
    // This would be called from main loop
}

uint64_t UsbBridge::clientReadFile(const std::string& clientId,
                                   ClientType clientType,
                                   const std::string& drivePath,
                                   std::function<void(const FileOperation&)> callback) {
    Logger::info("Client " + clientId + " requesting read: " + drivePath);
    
    // Queue the operation
    return m_operationQueue->queueRead(clientId, drivePath, 
        [this, callback](const FileOperation& op) {
            this->onOperationCompleted(op);
            if (callback) callback(op);
        });
}

uint64_t UsbBridge::clientWriteFile(const std::string& clientId,
                                    ClientType clientType,
                                    const std::string& localBufferPath,
                                    const std::string& driveDestPath,
                                    uint64_t fileSize,
                                    std::function<void(const FileOperation&)> callback) {
    Logger::info("Client " + clientId + " requesting write: " + driveDestPath + 
                 " (size: " + std::to_string(fileSize / (1024*1024)) + " MB)");
    
    // Queue the operation
    return m_operationQueue->queueWrite(clientId, localBufferPath, driveDestPath, fileSize,
        [this, callback](const FileOperation& op) {
            this->onOperationCompleted(op);
            if (callback) callback(op);
        });
}

uint64_t UsbBridge::clientDeleteFile(const std::string& clientId,
                                     ClientType clientType,
                                     const std::string& drivePath,
                                     std::function<void(const FileOperation&)> callback) {
    Logger::info("Client " + clientId + " requesting delete: " + drivePath);
    
    return m_operationQueue->queueDelete(clientId, drivePath,
        [this, callback](const FileOperation& op) {
            this->onOperationCompleted(op);
            if (callback) callback(op);
        });
}

uint64_t UsbBridge::clientCreateDirectory(const std::string& clientId,
                                          ClientType clientType,
                                          const std::string& drivePath,
                                          std::function<void(const FileOperation&)> callback) {
    Logger::info("Client " + clientId + " requesting mkdir: " + drivePath);
    
    return m_operationQueue->queueMkdir(clientId, drivePath,
        [this, callback](const FileOperation& op) {
            this->onOperationCompleted(op);
            if (callback) callback(op);
        });
}

uint64_t UsbBridge::clientMoveFile(const std::string& clientId,
                                   ClientType clientType,
                                   const std::string& driveSourcePath,
                                   const std::string& driveDestPath,
                                   std::function<void(const FileOperation&)> callback) {
    Logger::info("Client " + clientId + " requesting move: " + driveSourcePath + 
                 " -> " + driveDestPath);
    
    return m_operationQueue->queueMove(clientId, driveSourcePath, driveDestPath,
        [this, callback](const FileOperation& op) {
            this->onOperationCompleted(op);
            if (callback) callback(op);
        });
}

bool UsbBridge::cancelOperation(uint64_t operationId) {
    return m_operationQueue->cancelOperation(operationId);
}

OperationStatus UsbBridge::getOperationStatus(uint64_t operationId) const {
    return m_operationQueue->getOperationStatus(operationId);
}

std::shared_ptr<FileOperation> UsbBridge::getOperation(uint64_t operationId) const {
    return m_operationQueue->getOperation(operationId);
}

std::vector<std::shared_ptr<FileOperation>> UsbBridge::getQueuedOperations() const {
    return m_operationQueue->getQueuedOperations();
}

std::vector<std::shared_ptr<FileOperation>> UsbBridge::getClientOperations(
    const std::string& clientId) const {
    return m_operationQueue->getClientOperations(clientId);
}

bool UsbBridge::requestDirectAccess(const std::string& clientId,
                                    ClientType clientType,
                                    uint64_t operationId,
                                    std::chrono::seconds timeout) {
    Logger::info("Client " + clientId + " requesting direct access for operation #" + 
                 std::to_string(operationId));
    
    // Pause the operation queue while direct access is active
    m_operationQueue->pause();
    
    bool granted = m_mutexLocker->requestDirectAccess(clientId, clientType, operationId, timeout);
    
    if (granted) {
        switchToDirectAccessMode(clientId, clientType);
    } else {
        // Resume queue if access wasn't granted
        m_operationQueue->resume();
    }
    
    return granted;
}

void UsbBridge::releaseDirectAccess(const std::string& clientId) {
    Logger::info("Client " + clientId + " releasing direct access");
    
    m_mutexLocker->releaseDirectAccess(clientId);
    switchToBoardManagedMode();
    
    // Resume the operation queue
    m_operationQueue->resume();
}

void UsbBridge::switchToDirectAccessMode(const std::string& clientId, ClientType type) {
    Logger::info("Switching to direct access mode for client " + clientId);
    
    // Depending on client type, configure USB gadget or network share for direct access
    if (type == ClientType::USB_HOST_1 || type == ClientType::USB_HOST_2) {
        // Enable USB mass storage gadget mode
        // This would involve running the USB gadget setup script
        Logger::info("Enabling USB mass storage gadget for direct access");
    } else {
        // Network clients can access directly via SMB/HTTP without mode change
        Logger::info("Network client has direct access via existing shares");
    }
}

void UsbBridge::switchToBoardManagedMode() {
    Logger::info("Switching back to board-managed mode");
    
    // Disable USB gadget mode if it was enabled
    // Mount drive back for board access
    Logger::info("Board regaining drive control");
}

void UsbBridge::cleanupOldOperations() {
    m_operationQueue->cleanupCompletedOperations(m_operationCleanupAge);
}

void UsbBridge::checkDriveHealth() {
    if (m_storage) {
        // Check SMART status, file system errors, etc.
        // This is a placeholder for actual health monitoring
    }
}

void UsbBridge::updateSystemStatus() {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    
    m_status.currentAccessMode = m_mutexLocker->getCurrentAccessMode();
    m_status.accessHolder = m_mutexLocker->getCurrentAccessHolder();
    
    auto queuedOps = m_operationQueue->getQueuedOperations();
    m_status.queuedOperations = queuedOps.size();
    
    m_status.availableBufferSpace = m_operationQueue->getAvailableBufferSpace();
    m_status.usedBufferSpace = m_operationQueue->getUsedBufferSpace();
    
    if (m_storage) {
        m_status.driveConnected = m_storage->isDriveConnected();
        if (m_status.driveConnected) {
            auto storageInfo = m_storage->getStorageInfo();
            m_status.driveCapacity = storageInfo.totalSpace;
            m_status.driveUsed = storageInfo.usedSpace;
            m_status.driveFree = storageInfo.freeSpace;
            m_status.driveMountPoint = storageInfo.mountPoint;
            m_status.driveFilesystem = storageInfo.filesystem;
        }
    }
    
    if (m_hostController) {
        m_status.usbHost1Connected = m_hostController->isHostConnected(0);
        m_status.usbHost2Connected = m_hostController->isHostConnected(1);
    }
    
    if (m_network) {
        m_status.networkActive = m_network->isConnected();
    }
    
    m_status.smbServerRunning = m_smbServer && m_smbServer->isRunning();
    m_status.httpServerRunning = m_httpServer && m_httpServer->isRunning();
}

SystemStatus UsbBridge::getStatus() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_status;
}

bool UsbBridge::isRunning() const {
    return m_running;
}

void UsbBridge::onDriveConnected(const std::string& mountPoint) {
    Logger::info("Drive connected at: " + mountPoint);
    
    if (m_fileLogger) {
        m_fileLogger->logEvent("DRIVE_CONNECTED", mountPoint, "");
    }
    
    updateSystemStatus();
}

void UsbBridge::onDriveDisconnected() {
    Logger::warn("Drive disconnected");
    
    // Block all access
    m_mutexLocker->blockAccess("Drive disconnected");
    
    // Pause operation queue
    m_operationQueue->pause();
    
    if (m_fileLogger) {
        m_fileLogger->logEvent("DRIVE_DISCONNECTED", "", "");
    }
    
    updateSystemStatus();
}

void UsbBridge::onClientConnected(const std::string& clientId, ClientType type) {
    Logger::info("Client connected: " + clientId);
    
    if (m_fileLogger) {
        m_fileLogger->logEvent("CLIENT_CONNECTED", clientId, "");
    }
    
    updateSystemStatus();
}

void UsbBridge::onClientDisconnected(const std::string& clientId, ClientType type) {
    Logger::info("Client disconnected: " + clientId);
    
    // Release any direct access held by this client
    if (m_mutexLocker->hasDirectAccess(clientId)) {
        releaseDirectAccess(clientId);
    }
    
    if (m_fileLogger) {
        m_fileLogger->logEvent("CLIENT_DISCONNECTED", clientId, "");
    }
    
    updateSystemStatus();
}

void UsbBridge::onOperationCompleted(const FileOperation& operation) {
    Logger::info("Operation #" + std::to_string(operation.id) + " completed with status: " +
                 std::to_string(static_cast<int>(operation.status)));
    
    // Check if operation requires direct access
    if (operation.status == OperationStatus::DIRECT_ACCESS_REQUIRED) {
        onDirectAccessRequired(operation);
    }
    
    // Log the operation
    if (m_fileLogger) {
        std::string opType;
        switch (operation.type) {
            case OperationType::READ: opType = "READ"; break;
            case OperationType::WRITE: opType = "WRITE"; break;
            case OperationType::DELETE: opType = "DELETE"; break;
            case OperationType::MKDIR: opType = "MKDIR"; break;
            case OperationType::MOVE: opType = "MOVE"; break;
        }
        
        m_fileLogger->logEvent(opType, operation.sourcePath, operation.destPath);
    }
    
    updateSystemStatus();
}

void UsbBridge::onDirectAccessRequired(const FileOperation& operation) {
    Logger::warn("Operation #" + std::to_string(operation.id) + 
                 " requires direct access (file too large for buffer)");
    
    // Notify client that they need to request direct access
    // This would typically be done through a callback or event system
}

bool UsbBridge::isLargeFile(uint64_t fileSize) const {
    return fileSize > m_largeFileThreshold;
}

// Component accessors
ConfigManager& UsbBridge::getConfig() { return *m_config; }
const ConfigManager& UsbBridge::getConfig() const { return *m_config; }
StorageManager& UsbBridge::getStorageManager() { return *m_storage; }
HostController& UsbBridge::getHostController() { return *m_hostController; }
MutexLocker& UsbBridge::getMutexLocker() { return *m_mutexLocker; }
FileChangeLogger& UsbBridge::getFileLogger() { return *m_fileLogger; }
FileOperationQueue& UsbBridge::getOperationQueue() { return *m_operationQueue; }
NetworkManager& UsbBridge::getNetworkManager() { return *m_network; }
SmbServer& UsbBridge::getSmbServer() { return *m_smbServer; }
HttpServer& UsbBridge::getHttpServer() { return *m_httpServer; }
GuiManager& UsbBridge::getGuiManager() { return *m_gui; }

} // namespace usb_bridge