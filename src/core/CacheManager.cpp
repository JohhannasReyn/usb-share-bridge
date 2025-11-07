#include "core/CacheManager.hpp"
#include "utils/Logger.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace usb_bridge {

CacheManager::CacheManager(const std::string& cacheDir, uint64_t maxCacheSize)
    : m_cacheDir(cacheDir)
    , m_maxCacheSize(maxCacheSize)
    , m_currentCacheSize(0)
    , m_evictionPolicy("LRU")
    , m_prefetchEnabled(false)
    , m_nextEntryId(1)
    , m_stats({0, 0, 0, 0, 0, 0, maxCacheSize, 0.0, 0.0})
{
    Logger::info("CacheManager initialized with cache dir: " + m_cacheDir + 
                 ", max size: " + std::to_string(maxCacheSize / (1024*1024)) + " MB");
}

CacheManager::~CacheManager() {
    shutdown();
}

bool CacheManager::initialize() {
    try {
        // Create cache directory if it doesn't exist
        fs::create_directories(m_cacheDir);
        
        // Calculate current cache usage from existing files
        m_currentCacheSize = 0;
        for (const auto& entry : fs::directory_iterator(m_cacheDir)) {
            if (entry.is_regular_file()) {
                m_currentCacheSize += entry.file_size();
            }
        }
        
        Logger::info("Cache initialized, current usage: " + 
                    std::to_string(m_currentCacheSize / (1024*1024)) + " MB");
        
        return true;
    } catch (const std::exception& e) {
        Logger::error("Failed to initialize cache: " + std::string(e.what()));
        return false;
    }
}

void CacheManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Clean up cache entries
    Logger::info("Shutting down CacheManager, " + std::to_string(m_entries.size()) + " entries cached");
    
    m_entries.clear();
    m_idToPath.clear();
}

bool CacheManager::cacheFile(const std::string& drivePath, const std::string& cachePath, uint64_t fileSize) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if already cached
    if (m_entries.find(drivePath) != m_entries.end()) {
        Logger::warn("File already cached: " + drivePath);
        updateAccessTime(drivePath);
        return true;
    }
    
    // Check if we have enough space
    if (!hasEnoughSpace(fileSize)) {
        // Try to evict to make space
        if (!evictLRU(fileSize)) {
            Logger::error("Insufficient cache space and eviction failed for: " + drivePath);
            m_stats.totalCacheMisses++;
            return false;
        }
    }
    
    // Create cache entry
    auto entry = std::make_shared<CacheEntry>();
    entry->id = nextEntryId();
    entry->drivePath = drivePath;
    entry->cachePath = cachePath;
    entry->fileSize = fileSize;
    entry->state = CacheEntryState::READY;
    entry->createdTime = std::chrono::system_clock::now();
    entry->lastAccessTime = entry->createdTime;
    entry->lastModifiedTime = entry->createdTime;
    entry->accessCount = 1;
    entry->referenceCount = 0;
    entry->pinned = false;
    
    m_entries[drivePath] = entry;
    m_idToPath[entry->id] = drivePath;
    m_currentCacheSize += fileSize;
    
    m_stats.currentEntries++;
    m_stats.currentSize = m_currentCacheSize;
    m_stats.totalCacheHits++;
    
    Logger::info("Cached file: " + drivePath + " (" + std::to_string(fileSize / (1024*1024)) + " MB)");
    
    updateStatistics();
    return true;
}

bool CacheManager::uncacheFile(const std::string& drivePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    auto& entry = it->second;
    
    // Don't evict if still referenced
    if (entry->referenceCount > 0) {
        Logger::warn("Cannot uncache file with active references: " + drivePath);
        return false;
    }
    
    // Don't evict if pinned
    if (entry->pinned) {
        Logger::warn("Cannot uncache pinned file: " + drivePath);
        return false;
    }
    
    // Remove cache file if it exists
    try {
        if (fs::exists(entry->cachePath)) {
            fs::remove(entry->cachePath);
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to remove cache file: " + std::string(e.what()));
    }
    
    m_currentCacheSize -= entry->fileSize;
    m_idToPath.erase(entry->id);
    m_entries.erase(it);
    
    m_stats.currentEntries--;
    m_stats.currentSize = m_currentCacheSize;
    m_stats.totalEvictions++;
    
    Logger::info("Uncached file: " + drivePath);
    
    updateStatistics();
    return true;
}

bool CacheManager::isCached(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.find(drivePath) != m_entries.end();
}

std::string CacheManager::getCachePath(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return "";
    }
    
    return it->second->cachePath;
}

std::shared_ptr<CacheEntry> CacheManager::getCacheEntry(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return nullptr;
    }
    
    return it->second;
}

bool CacheManager::markDirty(const std::string& drivePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    it->second->state = CacheEntryState::DIRTY;
    it->second->lastModifiedTime = std::chrono::system_clock::now();
    
    Logger::debug("Marked file as dirty: " + drivePath);
    return true;
}

bool CacheManager::markClean(const std::string& drivePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    it->second->state = CacheEntryState::READY;
    m_stats.totalWritebacks++;
    
    Logger::debug("Marked file as clean: " + drivePath);
    return true;
}

bool CacheManager::isDirty(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    return it->second->state == CacheEntryState::DIRTY;
}

bool CacheManager::acquireReference(const std::string& drivePath, const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    auto& entry = it->second;
    entry->referenceCount++;
    
    // Add client to list if not already there
    if (std::find(entry->clientIds.begin(), entry->clientIds.end(), clientId) == entry->clientIds.end()) {
        entry->clientIds.push_back(clientId);
    }
    
    updateAccessTime(drivePath);
    
    Logger::debug("Acquired reference for " + drivePath + " by client " + clientId + 
                 " (count: " + std::to_string(entry->referenceCount) + ")");
    return true;
}

bool CacheManager::releaseReference(const std::string& drivePath, const std::string& clientId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    auto& entry = it->second;
    if (entry->referenceCount > 0) {
        entry->referenceCount--;
    }
    
    // Remove client from list
    auto clientIt = std::find(entry->clientIds.begin(), entry->clientIds.end(), clientId);
    if (clientIt != entry->clientIds.end()) {
        entry->clientIds.erase(clientIt);
    }
    
    Logger::debug("Released reference for " + drivePath + " by client " + clientId + 
                 " (count: " + std::to_string(entry->referenceCount) + ")");
    return true;
}

uint32_t CacheManager::getReferenceCount(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return 0;
    }
    
    return it->second->referenceCount;
}

bool CacheManager::pinFile(const std::string& drivePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    it->second->pinned = true;
    Logger::info("Pinned file: " + drivePath);
    return true;
}

bool CacheManager::unpinFile(const std::string& drivePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    it->second->pinned = false;
    Logger::info("Unpinned file: " + drivePath);
    return true;
}

bool CacheManager::isPinned(const std::string& drivePath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_entries.find(drivePath);
    if (it == m_entries.end()) {
        return false;
    }
    
    return it->second->pinned;
}

bool CacheManager::hasSpace(uint64_t requiredSize) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_currentCacheSize + requiredSize) <= m_maxCacheSize;
}

uint64_t CacheManager::getAvailableSpace() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxCacheSize > m_currentCacheSize ? m_maxCacheSize - m_currentCacheSize : 0;
}

uint64_t CacheManager::getUsedSpace() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentCacheSize;
}

uint64_t CacheManager::getTotalSpace() const {
    return m_maxCacheSize;
}

bool CacheManager::evictLRU(uint64_t requiredSpace) {
    // Already locked by caller
    
    auto candidates = selectEvictionCandidates(requiredSpace);
    
    if (candidates.empty()) {
        Logger::error("No eviction candidates found");
        return false;
    }
    
    Logger::info("Evicting " + std::to_string(candidates.size()) + " files to free " + 
                std::to_string(requiredSpace / (1024*1024)) + " MB");
    
    for (const auto& path : candidates) {
        uncacheFile(path);
    }
    
    return hasEnoughSpace(requiredSpace);
}

bool CacheManager::evictFile(const std::string& drivePath) {
    return uncacheFile(drivePath);
}

void CacheManager::setEvictionPolicy(const std::string& policy) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_evictionPolicy = policy;
    Logger::info("Eviction policy set to: " + policy);
}

std::vector<std::string> CacheManager::getDirtyFiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> dirtyFiles;
    for (const auto& pair : m_entries) {
        if (pair.second->state == CacheEntryState::DIRTY) {
            dirtyFiles.push_back(pair.first);
        }
    }
    
    return dirtyFiles;
}

std::vector<std::string> CacheManager::getEvictionCandidates(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> candidates;
    
    for (const auto& pair : m_entries) {
        if (pair.second->referenceCount == 0 && !pair.second->pinned) {
            candidates.push_back({pair.first, pair.second->lastAccessTime});
        }
    }
    
    // Sort by LRU
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    
    std::vector<std::string> result;
    for (size_t i = 0; i < std::min(maxCount, candidates.size()); ++i) {
        result.push_back(candidates[i].first);
    }
    
    return result;
}

void CacheManager::clearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    Logger::warn("Clearing entire cache!");
    
    std::vector<std::string> toRemove;
    for (const auto& pair : m_entries) {
        if (pair.second->referenceCount == 0 && !pair.second->pinned) {
            toRemove.push_back(pair.first);
        }
    }
    
    for (const auto& path : toRemove) {
        uncacheFile(path);
    }
}

void CacheManager::enablePrefetch(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_prefetchEnabled = enable;
    Logger::info("Prefetch " + std::string(enable ? "enabled" : "disabled"));
}

void CacheManager::prefetchFiles(const std::vector<std::string>& drivePaths) {
    if (!m_prefetchEnabled) {
        return;
    }
    
    Logger::info("Prefetch requested for " + std::to_string(drivePaths.size()) + " files");
    // Implementation would coordinate with FileOperationQueue to prefetch files
}

CacheManager::Statistics CacheManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

std::vector<std::shared_ptr<CacheEntry>> CacheManager::getAllEntries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<CacheEntry>> result;
    for (const auto& pair : m_entries) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<std::shared_ptr<CacheEntry>> CacheManager::getClientEntries(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<CacheEntry>> result;
    for (const auto& pair : m_entries) {
        if (std::find(pair.second->clientIds.begin(), pair.second->clientIds.end(), clientId) 
            != pair.second->clientIds.end()) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

uint64_t CacheManager::nextEntryId() {
    return m_nextEntryId++;
}

std::string CacheManager::allocateCachePath(const std::string& drivePath) {
    // Generate unique cache filename based on drive path and timestamp
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Extract filename from drive path
    fs::path path(drivePath);
    std::string filename = path.filename().string();
    
    return m_cacheDir + "/cache_" + std::to_string(timestamp) + "_" + filename;
}

bool CacheManager::hasEnoughSpace(uint64_t requiredSize) const {
    return (m_currentCacheSize + requiredSize) <= m_maxCacheSize;
}

void CacheManager::updateAccessTime(const std::string& drivePath) {
    auto it = m_entries.find(drivePath);
    if (it != m_entries.end()) {
        it->second->lastAccessTime = std::chrono::system_clock::now();
        it->second->accessCount++;
    }
}

void CacheManager::updateStatistics() {
    uint64_t totalHits = m_stats.totalCacheHits;
    uint64_t totalMisses = m_stats.totalCacheMisses;
    
    if (totalHits + totalMisses > 0) {
        m_stats.hitRate = static_cast<double>(totalHits) / (totalHits + totalMisses);
    }
}

std::vector<std::string> CacheManager::selectEvictionCandidates(uint64_t requiredSpace) {
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> candidates;
    
    // Collect eviction candidates (not pinned, not referenced)
    for (const auto& pair : m_entries) {
        if (pair.second->referenceCount == 0 && !pair.second->pinned) {
            candidates.push_back({pair.first, pair.second->lastAccessTime});
        }
    }
    
    if (candidates.empty()) {
        return {};
    }
    
    // Sort by LRU (oldest first)
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    
    // Select files until we have enough space
    std::vector<std::string> selected;
    uint64_t freedSpace = 0;
    
    for (const auto& candidate : candidates) {
        auto it = m_entries.find(candidate.first);
        if (it != m_entries.end()) {
            selected.push_back(candidate.first);
            freedSpace += it->second->fileSize;
            
            if (freedSpace >= requiredSpace) {
                break;
            }
        }
    }
    
    return selected;
}

} // namespace usb_bridge