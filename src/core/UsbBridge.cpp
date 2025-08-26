#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"
#include "core/ConfigManager.hpp"
#include <chrono>
#include <thread>

UsbBridge::UsbBridge() 
    : m_status(Status::IDLE)
    , m_running(false)
{
}

UsbBridge::~UsbBridge() {
    stop();
}

bool UsbBridge::initialize() {
    LOG_INFO("Initializing USB Bridge", "BRIDGE");
    
    m_status = Status::INITIALIZING;
    
    try {
        // Initialize storage manager
        m_storageManager = std::make_unique<StorageManager>();
        if (!m_storageManager->initialize()) {
            LOG_ERROR("Failed to initialize storage manager", "BRIDGE");
            m_status = Status::ERROR;
            return false;
        }
        
        // Initialize file change logger
        m_fileLogger = std::make_unique<FileChangeLogger>();
        // Will be initialized when storage is mounted
        
        // Initialize network manager
        m_networkManager = std::make_unique<NetworkManager>();
        if (!m_networkManager->initialize()) {
            LOG_WARNING("Failed to initialize network manager", "BRIDGE");
            // Continue without network - not critical
        }
        
        // Initialize USB host controllers
        int maxHosts = ConfigManager::instance().getIntValue("usb.max_hosts", 2);
        for (int i = 0; i < maxHosts; i++) {
            auto hostController = std::make_unique<HostController>(i);
            hostController->setStatusCallback([this](int hostId, HostController::ConnectionStatus status) {
                LOG_INFO("Host " + std::to_string(hostId) + " status: " + std::to_string((int)status), "BRIDGE");
                updateStatus();
            });
            m_hostControllers.push_back(std::move(hostController));
        }
        
        m_status = Status::READY;
        LOG_INFO("USB Bridge initialized successfully", "BRIDGE");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during initialization: " + std::string(e.what()), "BRIDGE");
        m_status = Status::ERROR;
        return false;
    }
}

void UsbBridge::start() {
    if (m_running) {
        return;
    }
    
    LOG_INFO("Starting USB Bridge", "BRIDGE");
    m_running = true;
    m_mainThread = std::thread(&UsbBridge::mainLoop, this);
    
    // Start storage monitoring
    m_storageManager->startMonitoring();
}

void UsbBridge::stop() {
    if (!m_running) {
        return;
    }
    
    LOG_INFO("Stopping USB Bridge", "BRIDGE");
    m_running = false;
    
    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }
    
    // Stop all services
    disableNetworkSharing();
    
    // Disconnect all USB hosts
    for (auto& host : m_hostControllers) {
        host->disconnect();
    }
    
    // Stop storage monitoring
    if (m_storageManager) {
        m_storageManager->stopMonitoring();
    }
    
    if (m_fileLogger) {
        m_fileLogger->stopLogging();
    }
    
    m_status = Status::IDLE;
}

void UsbBridge::mainLoop() {
    LOG_INFO("Main loop started", "BRIDGE");
    
    while (m_running) {
        updateStatus();
        
        // Check for drive changes
        static bool lastDriveState = false;
        bool currentDriveState = m_storageManager->isDriveConnected();
        
        if (currentDriveState != lastDriveState) {
            if (currentDriveState) {
                LOG_INFO("External drive connected", "BRIDGE");
                // Initialize file logger for the mounted drive
                std::string mountPoint = m_storageManager->getDriveInfo().mountPoint;
                m_fileLogger->initialize(mountPoint);
                m_fileLogger->startLogging();
            } else {
                LOG_INFO("External drive disconnected", "BRIDGE");
                m_fileLogger->stopLogging();
            }
            lastDriveState = currentDriveState;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_INFO("Main loop ended", "BRIDGE");
}

void UsbBridge::updateStatus() {
    Status newStatus = Status::READY;
    
    // Check if any USB hosts are connected
    bool usbConnected = false;
    for (const auto& host : m_hostControllers) {
        if (host->isConnected()) {
            usbConnected = true;
            break;
        }
    }
    
    // Check network status
    bool networkActive = m_networkManager && m_networkManager->areServicesRunning();
    
    if (usbConnected && networkActive) {
        newStatus = Status::NETWORK_ACTIVE;
    } else if (usbConnected) {
        newStatus = Status::USB_CONNECTED;
    } else if (networkActive) {
        newStatus = Status::NETWORK_ACTIVE;
    }
    
    if (!m_storageManager->isDriveConnected()) {
        newStatus = Status::IDLE;
    }
    
    m_status = newStatus;
}

bool UsbBridge::connectUsbHost(int hostId) {
    if (hostId >= 0 && hostId < m_hostControllers.size()) {
        return m_hostControllers[hostId]->connect();
    }
    return false;
}

bool UsbBridge::disconnectUsbHost(int hostId) {
    if (hostId >= 0 && hostId < m_hostControllers.size()) {
        return m_hostControllers[hostId]->disconnect();
    }
    return false;
}

std::vector<int> UsbBridge::getConnectedHosts() const {
    std::vector<int> connectedHosts;
    for (size_t i = 0; i < m_hostControllers.size(); i++) {
        if (m_hostControllers[i]->isConnected()) {
            connectedHosts.push_back(i);
        }
    }
    return connectedHosts;
}

bool UsbBridge::enableNetworkSharing() {
    if (!m_networkManager || !m_storageManager->isDriveConnected()) {
        return false;
    }
    
    LOG_INFO("Enabling network sharing", "BRIDGE");
    return m_networkManager->startNetworkServices();
}

bool UsbBridge::disableNetworkSharing() {
    if (!m_networkManager) {
        return false;
    }
    
    LOG_INFO("Disabling network sharing", "BRIDGE");
    return m_networkManager->stopNetworkServices();
}

bool UsbBridge::isNetworkActive() const {
    return m_networkManager && m_networkManager->areServicesRunning();
}
