#pragma once

#include <mutex>
#include <string>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace usb_bridge {

enum class AccessMode {
    NONE,              // No client has access
    BOARD_MANAGED,     // Board maintains access and handles operations via queue
    DIRECT_USB,        // Client has direct USB access (for large files)
    DIRECT_NETWORK     // Client has direct network access (for large files)
};

enum class ClientType {
    USB_HOST_1,
    USB_HOST_2,
    NETWORK_SMB,
    NETWORK_HTTP,
    SYSTEM
};

struct AccessGrant {
    std::string clientId;
    ClientType clientType;
    AccessMode mode;
    std::chrono::system_clock::time_point grantedTime;
    std::chrono::system_clock::time_point expiryTime;
    uint64_t operationId;  // Associated operation ID if any
    bool isActive;
};

class MutexLocker {
public:
    MutexLocker();
    ~MutexLocker();

    // Normal operation mode - board maintains drive access
    // All file operations go through the queue
    bool isBoardManaged() const;
    
    // Request direct access for large file operations
    // Returns true if direct access granted, false if needs to wait or denied
    bool requestDirectAccess(const std::string& clientId, 
                            ClientType clientType,
                            uint64_t operationId,
                            std::chrono::seconds timeout = std::chrono::seconds(30));
    
    // Release direct access and return to board-managed mode
    void releaseDirectAccess(const std::string& clientId);
    
    // Check if a specific client has direct access
    bool hasDirectAccess(const std::string& clientId) const;
    
    // Get current access mode
    AccessMode getCurrentAccessMode() const;
    
    // Get current access holder (if any)
    std::string getCurrentAccessHolder() const;
    
    // Force release of all access (emergency use)
    void forceReleaseAll();
    
    // Check if drive is currently accessible for queue operations
    bool isDriveAccessible() const;
    
    // Temporarily block all access (for maintenance, unmounting, etc.)
    void blockAccess(const std::string& reason);
    void unblockAccess();
    bool isAccessBlocked() const;
    std::string getBlockReason() const;
    
    // Statistics
    struct Statistics {
        uint64_t totalDirectAccessRequests;
        uint64_t grantedDirectAccess;
        uint64_t deniedDirectAccess;
        uint64_t timeoutDirectAccess;
        std::chrono::milliseconds averageDirectAccessDuration;
        uint64_t currentQueuedRequests;
    };
    Statistics getStatistics() const;
    
    // Get all current access grants
    std::vector<AccessGrant> getActiveGrants() const;

private:
    bool grantDirectAccess(const std::string& clientId, 
                          ClientType clientType, 
                          uint64_t operationId);
    
    void cleanupExpiredGrants();
    
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    
    AccessMode m_currentMode;
    std::shared_ptr<AccessGrant> m_currentGrant;
    
    std::unordered_map<std::string, std::shared_ptr<AccessGrant>> m_accessHistory;
    
    bool m_blocked;
    std::string m_blockReason;
    
    Statistics m_stats;
};

// RAII helper class for automatic direct access management
class DirectAccessGuard {
public:
    DirectAccessGuard(MutexLocker& locker, 
                     const std::string& clientId,
                     ClientType clientType,
                     uint64_t operationId,
                     std::chrono::seconds timeout = std::chrono::seconds(30))
        : m_locker(locker)
        , m_clientId(clientId)
        , m_acquired(false)
    {
        m_acquired = m_locker.requestDirectAccess(clientId, clientType, operationId, timeout);
    }
    
    ~DirectAccessGuard() {
        if (m_acquired) {
            m_locker.releaseDirectAccess(m_clientId);
        }
    }
    
    bool isAcquired() const { return m_acquired; }
    
    // Non-copyable
    DirectAccessGuard(const DirectAccessGuard&) = delete;
    DirectAccessGuard& operator=(const DirectAccessGuard&) = delete;
    
    // Movable
    DirectAccessGuard(DirectAccessGuard&& other) noexcept
        : m_locker(other.m_locker)
        , m_clientId(std::move(other.m_clientId))
        , m_acquired(other.m_acquired)
    {
        other.m_acquired = false;
    }

private:
    MutexLocker& m_locker;
    std::string m_clientId;
    bool m_acquired;
};

} // namespace usb_bridge