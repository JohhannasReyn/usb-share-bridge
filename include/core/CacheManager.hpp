#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <functional>

namespace usb_bridge {

enum class CacheEntryState {
    EMPTY,          // Slot is empty
    LOADING,        // File is being loaded into cache
    READY,          // File is fully cached and ready
    DIRTY,          // File has been modified, needs write back
    WRITING_BACK,   // File is being written back to drive
    EVICTING        // File is being evicted from cache
};

struct CacheEntry {
    uint64_t id;
    std::string drivePath;      // Original path on drive
    std::string cachePath;      // Path in local cache (/data/buffer/cache/...)
    uint64_t fileSize;
    CacheEntryState state;
    std::chrono::system_clock::time_point createdTime;
    std::chrono::system_clock::time_point lastAccessTime;
    std::chrono::system_clock::time_point lastModifiedTime;
    uint32_t accessCount;
    uint32_t referenceCount;    // Number of active users
    bool pinned;                // Prevent eviction
    std::vector<std::string> clientIds;  // Clients using this entry
};

/**
 * CacheManager - Manages intelligent file caching for improved performance
 * 
 * Works in conjunction with FileOperationQueue to provide:
 * - Read caching for frequently accessed files
 * - Write caching with deferred writeback
 * - LRU eviction policy
 * - Cache pinning for important files
 * - Read-ahead and prefetching
 */
class CacheManager {
public:
    CacheManager(const std::string& cacheDir, uint64_t maxCacheSize);
    ~CacheManager();

    // Initialization
    bool initialize();
    void shutdown();

    // Cache operations
    bool cacheFile(const std::string& drivePath, const std::string& cachePath, uint64_t fileSize);
    bool uncacheFile(const std::string& drivePath);
    bool isCached(const std::string& drivePath) const;
    std::string getCachePath(const std::string& drivePath) const;
    std::shared_ptr<CacheEntry> getCacheEntry(const std::string& drivePath) const;

    // Cache state management
    bool markDirty(const std::string& drivePath);
    bool markClean(const std::string& drivePath);
    bool isDirty(const std::string& drivePath) const;

    // Reference counting (for active file usage)
    bool acquireReference(const std::string& drivePath, const std::string& clientId);
    bool releaseReference(const std::string& drivePath, const std::string& clientId);
    uint32_t getReferenceCount(const std::string& drivePath) const;

    // Pinning (prevent eviction)
    bool pinFile(const std::string& drivePath);
    bool unpinFile(const std::string& drivePath);
    bool isPinned(const std::string& drivePath) const;

    // Cache management
    bool hasSpace(uint64_t requiredSize) const;
    uint64_t getAvailableSpace() const;
    uint64_t getUsedSpace() const;
    uint64_t getTotalSpace() const;
    
    // Eviction
    bool evictLRU(uint64_t requiredSpace);
    bool evictFile(const std::string& drivePath);
    void setEvictionPolicy(const std::string& policy);  // "LRU", "LFU", "FIFO"

    // Batch operations
    std::vector<std::string> getDirtyFiles() const;
    std::vector<std::string> getEvictionCandidates(size_t maxCount) const;
    void clearCache();

    // Prefetching
    void enablePrefetch(bool enable);
    void prefetchFiles(const std::vector<std::string>& drivePaths);

    // Statistics
    struct Statistics {
        uint64_t totalCacheHits;
        uint64_t totalCacheMisses;
        uint64_t totalEvictions;
        uint64_t totalWritebacks;
        uint64_t currentEntries;
        uint64_t currentSize;
        uint64_t maxSize;
        double hitRate;
        double averageAccessTime;
    };
    Statistics getStatistics() const;

    // Monitoring
    std::vector<std::shared_ptr<CacheEntry>> getAllEntries() const;
    std::vector<std::shared_ptr<CacheEntry>> getClientEntries(const std::string& clientId) const;

private:
    uint64_t nextEntryId();
    std::string allocateCachePath(const std::string& drivePath);
    bool hasEnoughSpace(uint64_t requiredSize) const;
    void updateAccessTime(const std::string& drivePath);
    void updateStatistics();
    std::vector<std::string> selectEvictionCandidates(uint64_t requiredSpace);

    std::string m_cacheDir;
    uint64_t m_maxCacheSize;
    uint64_t m_currentCacheSize;

    // Entry management
    std::unordered_map<std::string, std::shared_ptr<CacheEntry>> m_entries;
    std::unordered_map<uint64_t, std::string> m_idToPath;

    mutable std::mutex m_mutex;

    // Configuration
    std::string m_evictionPolicy;
    bool m_prefetchEnabled;

    // Statistics
    Statistics m_stats;
    uint64_t m_nextEntryId;
};

/**
 * RAII helper for cache entry references
 */
class CacheEntryGuard {
public:
    CacheEntryGuard(CacheManager& manager, 
                   const std::string& drivePath,
                   const std::string& clientId)
        : m_manager(manager)
        , m_drivePath(drivePath)
        , m_clientId(clientId)
        , m_acquired(false)
    {
        m_acquired = m_manager.acquireReference(drivePath, clientId);
    }

    ~CacheEntryGuard() {
        if (m_acquired) {
            m_manager.releaseReference(m_drivePath, m_clientId);
        }
    }

    bool isAcquired() const { return m_acquired; }

    // Non-copyable
    CacheEntryGuard(const CacheEntryGuard&) = delete;
    CacheEntryGuard& operator=(const CacheEntryGuard&) = delete;

    // Movable
    CacheEntryGuard(CacheEntryGuard&& other) noexcept
        : m_manager(other.m_manager)
        , m_drivePath(std::move(other.m_drivePath))
        , m_clientId(std::move(other.m_clientId))
        , m_acquired(other.m_acquired)
    {
        other.m_acquired = false;
    }

private:
    CacheManager& m_manager;
    std::string m_drivePath;
    std::string m_clientId;
    bool m_acquired;
};

} // namespace usb_bridge