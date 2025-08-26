#include "core/StorageManager.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include <filesystem>
#include <fstream>
#include <regex>

StorageManager::StorageManager()
    : m_driveConnected(false)
    , m_accessible(true)
    , m_monitoring(false)
{
}

StorageManager::~StorageManager() {
    cleanup();
}

bool StorageManager::initialize() {
    LOG_INFO("Initializing Storage Manager", "STORAGE");
    
    // Create mount point if it doesn't exist
    m_mountPoint = "/mnt/usb_bridge";
    if (!FileUtils::directoryExists(m_mountPoint)) {
        if (!FileUtils::createDirectory(m_mountPoint)) {
            LOG_ERROR("Failed to create mount point: " + m_mountPoint, "STORAGE");
            return false;
        }
    }
    
    // Detect existing drives
    detectDrives();
    
    LOG_INFO("Storage Manager initialized", "STORAGE");
    return true;
}

void StorageManager::cleanup() {
    stopMonitoring();
    if (m_driveConnected) {
        unmountDrive();
    }
}

bool StorageManager::mountDrive(const std::string& devicePath) {
    LOG_INFO("Mounting drive: " + devicePath, "STORAGE");
    
    if (m_driveConnected) {
        LOG_WARNING("Drive already mounted, unmounting first", "STORAGE");
        unmountDrive();
    }
    
    // Mount the drive (simplified - in reality you'd use proper mount syscalls)
    std::string mountCmd = "mount " + devicePath + " " + m_mountPoint;
    int result = system(mountCmd.c_str());
    
    if (result == 0) {
        m_driveConnected = true;
        m_currentDrive.devicePath = devicePath;
        m_currentDrive.mountPoint = m_mountPoint;
        m_currentDrive.isMounted = true;
        
        // Get drive info
        try {
            auto space = std::filesystem::space(m_mountPoint);
            m_currentDrive.totalSpace = space.capacity;
            m_currentDrive.freeSpace = space.free;
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to get space info: " + std::string(e.what()), "STORAGE");
        }
        
        LOG_INFO("Drive mounted successfully", "STORAGE");
        return true;
    } else {
        LOG_ERROR("Failed to mount drive", "STORAGE");
        return false;
    }
}

bool StorageManager::unmountDrive() {
    if (!m_driveConnected) {
        return true;
    }
    
    LOG_INFO("Unmounting drive", "STORAGE");
    
    std::string unmountCmd = "umount " + m_mountPoint;
    int result = system(unmountCmd.c_str());
    
    if (result == 0) {
        m_driveConnected = false;
        m_currentDrive = DriveInfo{};
        LOG_INFO("Drive unmounted successfully", "STORAGE");
        return true;
    } else {
        LOG_ERROR("Failed to unmount drive", "STORAGE");
        return false;
    }
}

std::vector<FileInfo> StorageManager::listDirectory(const std::string& path) {
    std::vector<FileInfo> files;
    
    if (!m_driveConnected || !m_accessible) {
        return files;
    }
    
    std::string fullPath = path.empty() ? m_mountPoint : FileUtils::joinPath(m_mountPoint, path);
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(fullPath)) {
            FileInfo info;
            info.name = entry.path().filename().string();
            info.path = FileUtils::getRelativePath(entry.path().string(), m_mountPoint);
            info.isDirectory = entry.is_directory();
            
            if (!info.isDirectory) {
                info.size = entry.file_size();
                info.mimeType = FileUtils::getMimeType(entry.path().string());
            } else {
                info.size = 0;
            }
            
            auto ftime = entry.last_write_time();
            info.lastModified = std::chrono::system_clock::to_time_t(
                std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()));
            
            files.push_back(info);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to list directory: " + std::string(e.what()), "STORAGE");
    }
    
    return files;
}

FileInfo StorageManager::getFileInfo(const std::string& path) {
    FileInfo info;
    
    if (!m_driveConnected || !m_accessible) {
        return info;
    }
    
    std::string fullPath = FileUtils::joinPath(m_mountPoint, path);
    
    try {
        if (std::filesystem::exists(fullPath)) {
            auto entry = std::filesystem::directory_entry(fullPath);
            info.name = entry.path().filename().string();
            info.path = path;
            info.isDirectory = entry.is_directory();
            
            if (!info.isDirectory) {
                info.size = entry.file_size();
                info.mimeType = FileUtils::getMimeType(fullPath);
            }
            
            auto ftime = entry.last_write_time();
            info.lastModified = std::chrono::system_clock::to_time_t(
                std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get file info: " + std::string(e.what()), "STORAGE");
    }
    
    return info;
}

void StorageManager::detectDrives() {
    // Simplified drive detection - scan /dev for USB devices
    // In a real implementation, you'd use udev or similar
    
    std::vector<std::string> possibleDevices = {
        "/dev/sda1", "/dev/sdb1", "/dev/sdc1",
        "/dev/mmcblk0p1", "/dev/mmcblk1p1"
    };
    
    for (const auto& device : possibleDevices) {
        if (FileUtils::fileExists(device)) {
            LOG_INFO("Found potential drive: " + device, "STORAGE");
            // Try to mount it
            if (mountDrive(device)) {
                break;
            }
        }
    }
}

void StorageManager::startMonitoring() {
    if (m_monitoring) {
        return;
    }
    
    m_monitoring = true;
    m_monitorThread = std::thread(&StorageManager::monitorLoop, this);
    LOG_INFO("Started storage monitoring", "STORAGE");
}

void StorageManager::stopMonitoring() {
    if (!m_monitoring) {
        return;
    }
    
    m_monitoring = false;
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    LOG_INFO("Stopped storage monitoring", "STORAGE");
}

void StorageManager::monitorLoop() {
    while (m_monitoring) {
        // Check if drive is still accessible
        if (m_driveConnected) {
            try {
                std::filesystem::space(m_mountPoint);
                // Update free space
                auto space = std::filesystem::space(m_mountPoint);
                m_currentDrive.freeSpace = space.free;
            } catch (const std::exception&) {
                LOG_WARNING("Drive became inaccessible", "STORAGE");
                m_driveConnected = false;
                m_currentDrive = DriveInfo{};
            }
        } else {
            // Check for new drives
            detectDrives();
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}