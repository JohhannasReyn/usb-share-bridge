#include "core/MutexLocker.hpp"
#include "utils/Logger.hpp"

namespace usb_bridge {

MutexLocker::MutexLocker()
    : m_currentMode(AccessMode::BOARD_MANAGED)
    , m_currentGrant(nullptr)
    , m_blocked(false)
    , m_stats({0, 0, 0, 0, std::chrono::milliseconds(0), 0})
{
    Logger::info("MutexLocker initialized in BOARD_MANAGED mode");
}

MutexLocker::~MutexLocker() {
    forceReleaseAll();
}

bool MutexLocker::isBoardManaged() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentMode == AccessMode::BOARD_MANAGED;
}

bool MutexLocker::requestDirectAccess(const std::string& clientId, 
                                      ClientType clientType,
                                      uint64_t operationId,
                                      std::chrono::seconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    m_stats.totalDirectAccessRequests++;
    m_stats.currentQueuedRequests++;
    
    Logger::info("Client " + clientId + " requesting direct access for operation #" + 
                 std::to_string(operationId));
    
    // Check if access is blocked
    if (m_blocked) {
        Logger::warn("Direct access denied - system is blocked: " + m_blockReason);
        m_stats.deniedDirectAccess++;
        m_stats.currentQueuedRequests--;
        return false;
    }
    
    // Wait until we can grant access or timeout
    auto deadline = std::chrono::system_clock::now() + timeout;
    
    bool waitResult = m_condition.wait_until(lock, deadline, [this] {
        return m_currentMode == AccessMode::BOARD_MANAGED || m_blocked;
    });
    
    if (m_blocked) {
        Logger::warn("Direct access denied - system became blocked during wait");
        m_stats.deniedDirectAccess++;
        m_stats.currentQueuedRequests--;
        return false;
    }
    
    if (!waitResult) {
        Logger::warn("Direct access request timed out for client " + clientId);
        m_stats.timeoutDirectAccess++;
        m_stats.currentQueuedRequests--;
        return false;
    }
    
    // Grant access
    bool granted = grantDirectAccess(clientId, clientType, operationId);
    if (granted) {
        m_stats.grantedDirectAccess++;
        Logger::info("Direct access granted to client " + clientId);
    } else {
        m_stats.deniedDirectAccess++;
        Logger::warn("Failed to grant direct access to client " + clientId);
    }
    
    m_stats.currentQueuedRequests--;
    return granted;
}

bool MutexLocker::grantDirectAccess(const std::string& clientId, 
                                    ClientType clientType, 
                                    uint64_t operationId) {
    // Should only be called when m_currentMode is BOARD_MANAGED
    if (m_currentMode != AccessMode::BOARD_MANAGED) {
        return false;
    }
    
    auto grant = std::make_shared<AccessGrant>();
    grant->clientId = clientId;
    grant->clientType = clientType;
    grant->operationId = operationId;
    grant->grantedTime = std::chrono::system_clock::now();
    grant->expiryTime = grant->grantedTime + std::chrono::minutes(5); // 5 minute timeout
    grant->isActive = true;
    
    // Determine access mode based on client type
    if (clientType == ClientType::USB_HOST_1 || clientType == ClientType::USB_HOST_2) {
        grant->mode = AccessMode::DIRECT_USB;
        m_currentMode = AccessMode::DIRECT_USB;
    } else {
        grant->mode = AccessMode::DIRECT_NETWORK;
        m_currentMode = AccessMode::DIRECT_NETWORK;
    }
    
    m_currentGrant = grant;
    m_accessHistory[clientId] = grant;
    
    return true;
}

void MutexLocker::releaseDirectAccess(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_currentGrant || m_currentGrant->clientId != clientId) {
        Logger::warn("Attempted to release direct access by non-holder: " + clientId);
        return;
    }
    
    m_currentGrant->isActive = false;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - m_currentGrant->grantedTime);
    
    // Update statistics
    auto totalDuration = m_stats.averageDirectAccessDuration.count() * (m_stats.grantedDirectAccess - 1);
    totalDuration += duration.count();
    m_stats.averageDirectAccessDuration = std::chrono::milliseconds(
        totalDuration / m_stats.grantedDirectAccess);
    
    Logger::info("Client " + clientId + " released direct access (duration: " + 
                 std::to_string(duration.count()) + "ms)");
    
    // Return to board-managed mode
    m_currentMode = AccessMode::BOARD_MANAGED;
    m_currentGrant = nullptr;
    
    // Notify waiting clients
    m_condition.notify_all();
}

bool MutexLocker::hasDirectAccess(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentGrant && m_currentGrant->clientId == clientId && m_currentGrant->isActive;
}

AccessMode MutexLocker::getCurrentAccessMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentMode;
}

std::string MutexLocker::getCurrentAccessHolder() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentGrant && m_currentGrant->isActive) {
        return m_currentGrant->clientId;
    }
    return "BOARD";
}

void MutexLocker::forceReleaseAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_currentGrant && m_currentGrant->isActive) {
        Logger::warn("Force releasing direct access from " + m_currentGrant->clientId);
        m_currentGrant->isActive = false;
    }
    
    m_currentMode = AccessMode::BOARD_MANAGED;
    m_currentGrant = nullptr;
    
    m_condition.notify_all();
}

bool MutexLocker::isDriveAccessible() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_blocked && m_currentMode == AccessMode::BOARD_MANAGED;
}

void MutexLocker::blockAccess(const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_blocked = true;
    m_blockReason = reason;
    
    Logger::warn("Drive access blocked: " + reason);
    m_condition.notify_all();
}

void MutexLocker::unblockAccess() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_blocked) {
        Logger::info("Drive access unblocked (was: " + m_blockReason + ")");
        m_blocked = false;
        m_blockReason.clear();
        m_condition.notify_all();
    }
}

bool MutexLocker::isAccessBlocked() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blocked;
}

std::string MutexLocker::getBlockReason() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_blockReason;
}

void MutexLocker::cleanupExpiredGrants() {
    auto now = std::chrono::system_clock::now();
    
    if (m_currentGrant && m_currentGrant->isActive && 
        now > m_currentGrant->expiryTime) {
        Logger::warn("Direct access grant expired for " + m_currentGrant->clientId);
        releaseDirectAccess(m_currentGrant->clientId);
    }
}

MutexLocker::Statistics MutexLocker::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

std::vector<AccessGrant> MutexLocker::getActiveGrants() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<AccessGrant> grants;
    
    if (m_currentGrant && m_currentGrant->isActive) {
        grants.push_back(*m_currentGrant);
    }
    
    return grants;
}

} // namespace usb_bridge