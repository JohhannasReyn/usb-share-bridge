#pragma once

#include <string>
#include <atomic>
#include <thread>

class SmbServer {
public:
    SmbServer();
    ~SmbServer();

    bool initialize(const std::string& sharePath, const std::string& shareName = "USBShare");
    bool start();
    bool stop();
    bool isRunning() const { return m_running; }
    
    // Configuration
    void setShareName(const std::string& name) { m_shareName = name; }
    void setWorkgroup(const std::string& workgroup) { m_workgroup = workgroup; }
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    
    // Access control
    void setGuestAccess(bool enabled) { m_guestAccess = enabled; }
    bool addUser(const std::string& username, const std::string& password);
    void removeUser(const std::string& username);
    
    // Statistics
    int getConnectedClients() const;
    uint64_t getBytesTransferred() const;

private:
    void generateSmbConfig();
    bool startSambaServices();
    bool stopSambaServices();
    
    std::string m_sharePath;
    std::string m_shareName;
    std::string m_workgroup;
    std::atomic<bool> m_running;
    std::atomic<bool> m_readOnly;
    std::atomic<bool> m_guestAccess;
    
    static const std::string SMB_CONFIG_PATH;
};
