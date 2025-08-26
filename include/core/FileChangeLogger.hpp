#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

struct FileChangeEvent {
    enum Type {
        CREATED,
        MODIFIED,
        DELETED,
        MOVED
    };
    
    Type type;
    std::string path;
    std::string oldPath;  // For MOVED events
    std::chrono::system_clock::time_point timestamp;
    std::string hostId;   // Which USB host made the change
    uint64_t fileSize;
    
    nlohmann::json toJson() const;
    static FileChangeEvent fromJson(const nlohmann::json& json);
};

class FileChangeLogger {
public:
    FileChangeLogger();
    ~FileChangeLogger();

    bool initialize(const std::string& watchPath);
    void startLogging();
    void stopLogging();
    
    // Event access
    std::vector<FileChangeEvent> getRecentEvents(int limit = 100) const;
    std::vector<FileChangeEvent> getEventsSince(const std::chrono::system_clock::time_point& since) const;
    void clearOldEvents(const std::chrono::system_clock::time_point& before);
    
    // Statistics
    int getTotalEventCount() const;
    std::chrono::system_clock::time_point getLastEventTime() const;
    
    // Manual event logging (for network operations)
    void logEvent(const FileChangeEvent& event);

private:
    void monitorLoop();
    void scanForChanges();
    void loadStoredEvents();
    void saveEvents() const;
    std::string calculateFileHash(const std::string& path);
    
    std::string m_watchPath;
    std::atomic<bool> m_running;
    std::thread m_monitorThread;
    mutable std::mutex m_eventsMutex;
    
    std::vector<FileChangeEvent> m_events;
    std::map<std::string, std::string> m_fileHashes;  // path -> hash
    std::map<std::string, std::time_t> m_lastSeen;    // path -> timestamp
    
    static const std::string EVENTS_FILE_PATH;
    static const size_t MAX_STORED_EVENTS;
};
