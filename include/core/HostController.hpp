#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>

class HostController {
public:
    enum class ConnectionStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        ERROR
    };

    HostController(int hostId);
    ~HostController();

    bool connect();
    bool disconnect();
    bool isConnected() const { return m_status == ConnectionStatus::CONNECTED; }
    ConnectionStatus getStatus() const { return m_status; }
    int getHostId() const { return m_hostId; }
    
    // Callback for status changes
    void setStatusCallback(std::function<void(int, ConnectionStatus)> callback);
    
    // Access control
    void enableAccess();
    void disableAccess();
    bool hasAccess() const { return m_accessEnabled; }

private:
    void connectionLoop();
    void notifyStatusChange();
    
    int m_hostId;
    std::atomic<ConnectionStatus> m_status;
    std::atomic<bool> m_accessEnabled;
    std::atomic<bool> m_shouldRun;
    std::thread m_connectionThread;
    std::function<void(int, ConnectionStatus)> m_statusCallback;
};