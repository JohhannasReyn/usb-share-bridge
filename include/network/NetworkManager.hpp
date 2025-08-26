#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include "SmbServer.hpp"
#include "HttpServer.hpp"

struct NetworkInterface {
    std::string name;
    std::string ipAddress;
    std::string subnetMask;
    std::string gateway;
    bool isActive;
    bool isWireless;
};

struct WifiNetwork {
    std::string ssid;
    std::string security;  // WPA2, WPA3, OPEN
    int signalStrength;
    bool isConnected;
};

class NetworkManager {
public:
    enum class Status {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR
    };

    NetworkManager();
    ~NetworkManager();

    bool initialize();
    void cleanup();
    
    // Network interface management
    std::vector<NetworkInterface> getInterfaces() const;
    NetworkInterface getActiveInterface() const;
    Status getConnectionStatus() const { return m_status; }
    
    // WiFi management
    std::vector<WifiNetwork> scanWifiNetworks();
    bool connectToWifi(const std::string& ssid, const std::string& password);
    bool disconnectWifi();
    bool isWifiEnabled() const;
    void enableWifi();
    void disableWifi();
    std::string getCurrentWifiSSID() const;
    
    // Ethernet management
    bool isEthernetConnected() const;
    bool configureEthernet(const std::string& ip, const std::string& mask, const std::string& gateway);
    
    // Network services
    bool startNetworkServices();
    bool stopNetworkServices();
    bool areServicesRunning() const;
    
    SmbServer* getSmbServer() { return m_smbServer.get(); }
    HttpServer* getHttpServer() { return m_httpServer.get(); }
    
    // Status callbacks
    void setStatusCallback(std::function<void(Status)> callback);

private:
    void monitorConnection();
    void updateStatus();
    void notifyStatusChange();
    
    std::atomic<Status> m_status;
    std::atomic<bool> m_monitoring;
    std::thread m_monitorThread;
    
    std::unique_ptr<SmbServer> m_smbServer;
    std::unique_ptr<HttpServer> m_httpServer;
    std::function<void(Status)> m_statusCallback;
};
