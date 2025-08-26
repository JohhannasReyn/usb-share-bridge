#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include "StorageManager.hpp"
#include "HostController.hpp"
#include "FileChangeLogger.hpp"
#include "../network/NetworkManager.hpp"

class UsbBridge {
public:
    enum class Status {
        IDLE,
        INITIALIZING,
        READY,
        USB_CONNECTED,
        NETWORK_ACTIVE,
        ERROR
    };

    UsbBridge();
    ~UsbBridge();

    bool initialize();
    void start();
    void stop();
    Status getStatus() const { return m_status; }
    
    // USB Host management
    bool connectUsbHost(int hostId);
    bool disconnectUsbHost(int hostId);
    std::vector<int> getConnectedHosts() const;
    
    // Storage access
    StorageManager* getStorageManager() { return m_storageManager.get(); }
    FileChangeLogger* getFileLogger() { return m_fileLogger.get(); }
    
    // Network services
    bool enableNetworkSharing();
    bool disableNetworkSharing();
    bool isNetworkActive() const;

private:
    void mainLoop();
    void updateStatus();
    
    std::atomic<Status> m_status;
    std::atomic<bool> m_running;
    std::thread m_mainThread;
    
    std::unique_ptr<StorageManager> m_storageManager;
    std::vector<std::unique_ptr<HostController>> m_hostControllers;
    std::unique_ptr<FileChangeLogger> m_fileLogger;
    std::unique_ptr<NetworkManager> m_networkManager;
};