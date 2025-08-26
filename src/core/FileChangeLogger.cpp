#include "core/FileChangeLogger.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include <fstream>
#include <sys/inotify.h>
#include <unistd.h>

const std::string FileChangeLogger::EVENTS_FILE_PATH = "/data/recent_activity.json";
const size_t FileChangeLogger::MAX_STORED_EVENTS = 1000;

nlohmann::json FileChangeEvent::toJson() const {
    nlohmann::json j;
    j["type"] = static_cast<int>(type);
    j["path"] = path;
    j["old_path"] = oldPath;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    j["host_id"] = hostId;
    j["file_size"] = fileSize;
    return j;
}

FileChangeEvent FileChangeEvent::fromJson(const nlohmann::json& json) {
    FileChangeEvent event;
    event.type = static_cast<Type>(json.value("type", 0));
    event.path = json.value("path", "");
    event.oldPath = json.value("old_path", "");
    
    auto timestamp_ms = json.value("timestamp", 0);
    event.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp_ms));
    
    event.hostId = json.value("host_id", "");
    event.fileSize = json.value("file_size", 0);
    return event;
}

FileChangeLogger::FileChangeLogger()
    : m_running(false)
{
}

FileChangeLogger::~FileChangeLogger() {
    stopLogging();
}

bool FileChangeLogger::initialize(const std::string& watchPath) {
    LOG_INFO("Initializing file change logger for: " + watchPath, "FILELOG");
    
    m_watchPath = watchPath;
    
    // Load previously stored events
    loadStoredEvents();
    
    LOG_INFO("File change logger initialized", "FILELOG");
    return true;
}

void FileChangeLogger::startLogging() {
    if (m_running || m_watchPath.empty()) {
        return;
    }
    
    LOG_INFO("Starting file change monitoring", "FILELOG");
    
    m_running = true;
    m_monitorThread = std::thread(&FileChangeLogger::monitorLoop, this);
}

void FileChangeLogger::stopLogging() {
    if (!m_running) {
        return;
    }
    
    LOG_INFO("Stopping file change monitoring", "FILELOG");
    
    m_running = false;
    
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    
    // Save events before stopping
    saveEvents();
}

std::vector<FileChangeEvent> FileChangeLogger::getRecentEvents(int limit) const {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    
    std::vector<FileChangeEvent> result;
    int count = std::min(limit, static_cast<int>(m_events.size()));
    
    if (count > 0) {
        auto startIt = m_events.end() - count;
        result.assign(startIt, m_events.end());
        
        // Reverse to get most recent first
        std::reverse(result.begin(), result.end());
    }
    
    return result;
}

std::vector<FileChangeEvent> FileChangeLogger::getEventsSince(const std::chrono::system_clock::time_point& since) const {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    
    std::vector<FileChangeEvent> result;
    
    for (const auto& event : m_events) {
        if (event.timestamp >= since) {
            result.push_back(event);
        }
    }
    
    return result;
}

void FileChangeLogger::clearOldEvents(const std::chrono::system_clock::time_point& before) {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    
    auto it = std::remove_if(m_events.begin(), m_events.end(),
        [&before](const FileChangeEvent& event) {
            return event.timestamp < before;
        });
    
    m_events.erase(it, m_events.end());
    
    LOG_INFO("Cleared old file change events", "FILELOG");
    saveEvents();
}

int FileChangeLogger::getTotalEventCount() const {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    return static_cast<int>(m_events.size());
}

std::chrono::system_clock::time_point FileChangeLogger::getLastEventTime() const {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    
    if (m_events.empty()) {
        return std::chrono::system_clock::time_point{};
    }
    
    return m_events.back().timestamp;
}

void FileChangeLogger::logEvent(const FileChangeEvent& event) {
    std::lock_guard<std::mutex> lock(m_eventsMutex);
    
    m_events.push_back(event);
    
    // Limit the number of stored events
    if (m_events.size() > MAX_STORED_EVENTS) {
        m_events.erase(m_events.begin(), m_events.begin() + (m_events.size() - MAX_STORED_EVENTS));
    }
    
    LOG_DEBUG("Logged file change event: " + event.path, "FILELOG");
}

void FileChangeLogger::monitorLoop() {
    // Create inotify instance
    int inotifyFd = inotify_init();
    if (inotifyFd < 0) {
        LOG_ERROR("Failed to initialize inotify", "FILELOG");
        return;
    }
    
    // Add watch on the directory
    int watchDescriptor = inotify_add_watch(inotifyFd, m_watchPath.c_str(),
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    
    if (watchDescriptor < 0) {
        LOG_ERROR("Failed to add inotify watch", "FILELOG");
        close(inotifyFd);
        return;
    }
    
    LOG_INFO("File monitoring started using inotify", "FILELOG");
    
    // Initial scan to establish baseline
    scanForChanges();
    
    char buffer[4096];
    while (m_running) {
        // Set timeout for read
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(inotifyFd, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int result = select(inotifyFd + 1, &readfds, nullptr, nullptr, &timeout);
        
        if (result < 0) {
            LOG_ERROR("Error in select() for inotify", "FILELOG");
            break;
        } else if (result == 0) {
            // Timeout - continue loop
            continue;
        }
        
        // Read inotify events
        ssize_t bytesRead = read(inotifyFd, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            LOG_ERROR("Error reading inotify events", "FILELOG");
            break;
        }
        
        // Process events
        size_t offset = 0;
        while (offset < bytesRead) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(buffer + offset);
            
            if (event->len > 0) {
                std::string filename(event->name);
                std::string fullPath = FileUtils::joinPath(m_watchPath, filename);
                
                FileChangeEvent changeEvent;
                changeEvent.path = filename;
                changeEvent.timestamp = std::chrono::system_clock::now();
                changeEvent.hostId = "inotify"; // Could be enhanced to track actual host
                
                if (event->mask & IN_CREATE) {
                    changeEvent.type = FileChangeEvent::CREATED;
                    changeEvent.fileSize = FileUtils::getFileSize(fullPath);
                } else if (event->mask & IN_DELETE) {
                    changeEvent.type = FileChangeEvent::DELETED;
                    changeEvent.fileSize = 0;
                } else if (event->mask & IN_MODIFY) {
                    changeEvent.type = FileChangeEvent::MODIFIED;
                    changeEvent.fileSize = FileUtils::getFileSize(fullPath);
                } else if (event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) {
                    changeEvent.type = FileChangeEvent::MOVED;
                    changeEvent.fileSize = FileUtils::getFileSize(fullPath);
                }
                
                logEvent(changeEvent);
            }
            
            offset += sizeof(struct inotify_event) + event->len;
        }
        
        // Periodic save
        static auto lastSave = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::minutes>(now - lastSave).count() >= 5) {
            saveEvents();
            lastSave = now;
        }
    }
    
    // Cleanup
    inotify_rm_watch(inotifyFd, watchDescriptor);
    close(inotifyFd);
    
    LOG_INFO("File monitoring stopped", "FILELOG");
}

void FileChangeLogger::scanForChanges() {
    // This method performs a full directory scan to detect changes
    // It's used as a fallback when inotify events might be missed
    
    if (!FileUtils::directoryExists(m_watchPath)) {
        return;
    }
    
    auto files = FileUtils::listDirectory(m_watchPath);
    std::map<std::string, std::time_t> currentFiles;
    
    for (const auto& filename : files) {
        std::string fullPath = FileUtils::joinPath(m_watchPath, filename);
        std::time_t modTime = FileUtils::getLastModifiedTime(fullPath);
        currentFiles[filename] = modTime;
        
        // Check if this is a new file or modified file
        auto it = m_lastSeen.find(filename);
        if (it == m_lastSeen.end()) {
            // New file
            FileChangeEvent event;
            event.type = FileChangeEvent::CREATED;
            event.path = filename;
            event.timestamp = std::chrono::system_clock::now();
            event.hostId = "scan";
            event.fileSize = FileUtils::getFileSize(fullPath);
            
            logEvent(event);
        } else if (it->second != modTime) {
            // Modified file
            FileChangeEvent event;
            event.type = FileChangeEvent::MODIFIED;
            event.path = filename;
            event.timestamp = std::chrono::system_clock::now();
            event.hostId = "scan";
            event.fileSize = FileUtils::getFileSize(fullPath);
            
            logEvent(event);
        }
    }
    
    // Check for deleted files
    for (const auto& [filename, lastSeen] : m_lastSeen) {
        if (currentFiles.find(filename) == currentFiles.end()) {
            // File was deleted
            FileChangeEvent event;
            event.type = FileChangeEvent::DELETED;
            event.path = filename;
            event.timestamp = std::chrono::system_clock::now();
            event.hostId = "scan";
            event.fileSize = 0;
            
            logEvent(event);
        }
    }
    
    // Update last seen files
    m_lastSeen = currentFiles;
}

void FileChangeLogger::loadStoredEvents() {
    if (!FileUtils::fileExists(EVENTS_FILE_PATH)) {
        LOG_INFO("No stored events file found", "FILELOG");
        return;
    }
    
    try {
        std::string content = FileUtils::readTextFile(EVENTS_FILE_PATH);
        nlohmann::json eventsJson = nlohmann::json::parse(content);
        
        std::lock_guard<std::mutex> lock(m_eventsMutex);
        m_events.clear();
        
        for (const auto& eventJson : eventsJson["events"]) {
            FileChangeEvent event = FileChangeEvent::fromJson(eventJson);
            m_events.push_back(event);
        }
        
        LOG_INFO("Loaded " + std::to_string(m_events.size()) + " stored events", "FILELOG");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load stored events: " + std::string(e.what()), "FILELOG");
    }
}

void FileChangeLogger::saveEvents() const {
    try {
        nlohmann::json eventsJson;
        eventsJson["events"] = nlohmann::json::array();
        
        std::lock_guard<std::mutex> lock(m_eventsMutex);
        
        // Only save recent events to limit file size
        size_t startIndex = m_events.size() > MAX_STORED_EVENTS ? 
            m_events.size() - MAX_STORED_EVENTS : 0;
        
        for (size_t i = startIndex; i < m_events.size(); i++) {
            eventsJson["events"].push_back(m_events[i].toJson());
        }
        
        eventsJson["metadata"] = {
            {"saved_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"watch_path", m_watchPath},
            {"total_events", m_events.size()}
        };
        
        std::string content = eventsJson.dump(2);
        if (FileUtils::writeTextFile(EVENTS_FILE_PATH, content)) {
            LOG_DEBUG("Saved file change events to disk", "FILELOG");
        } else {
            LOG_ERROR("Failed to save file change events", "FILELOG");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize events: " + std::string(e.what()), "FILELOG");
    }
}

std::string FileChangeLogger::calculateFileHash(const std::string& path) {
    // Use MD5 for performance (this is just for change detection, not security)
    return FileUtils::calculateMD5(path);
}
