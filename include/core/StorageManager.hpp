#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <mutex>
#include "MutexLocker.hpp"

struct FileInfo {
    std::string name;
    std::string path;
    uint64_t size;
    bool isDirectory;
    std::time_t lastModified;
    std::string mimeType;
};

struct DriveInfo {
    std::string devicePath;
    std::string mountPoint;
    std::string fileSystem;
    uint64_t totalSpace;
    uint64_t freeSpace;
    bool isMounted;
};

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool initialize();
    void cleanup();
    
    // Drive management
    bool mountDrive(const std::string& devicePath);
    bool unmountDrive();
    bool isDriveConnected() const { return m_driveConnected; }
    DriveInfo getDriveInfo() const;
    
    // File operations (read-only for UI)
    std::vector<FileInfo> listDirectory(const std::string& path = "");
    FileInfo getFileInfo(const std::string& path);
    bool fileExists(const std::string& path);
    std::string getAbsolutePath(const std::string& relativePath);
    
    // Access control
    bool isAccessible() const { return m_accessible; }
    void setAccessible(bool accessible) { m_accessible = accessible; }
    
    // Monitoring
    void startMonitoring();
    void stopMonitoring();

private:
    void detectDrives();
    void monitorLoop();
    
    mutable std::mutex m_mutex;
    std::string m_mountPoint;
    std::atomic<bool> m_driveConnected;
    std::atomic<bool> m_accessible;
    std::atomic<bool> m_monitoring;
    std::thread m_monitorThread;
    DriveInfo m_currentDrive;
};