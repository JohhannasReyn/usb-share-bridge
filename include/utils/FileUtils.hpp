#pragma once

#include <ctime>
#include <string>
#include <vector>
#include <cstdint>

namespace FileUtils {
    // File operations
    bool fileExists(const std::string& path);
    bool directoryExists(const std::string& path);
    bool createDirectory(const std::string& path);
    bool removeFile(const std::string& path);
    bool removeDirectory(const std::string& path);
    
    // File info
    uint64_t getFileSize(const std::string& path);
    std::time_t getLastModifiedTime(const std::string& path);
    std::string getFileExtension(const std::string& path);
    std::string getFileName(const std::string& path);
    std::string getDirectoryPath(const std::string& path);
    
    // MIME type detection
    std::string getMimeType(const std::string& path);
    bool isImageFile(const std::string& path);
    bool isVideoFile(const std::string& path);
    bool isAudioFile(const std::string& path);
    bool isTextFile(const std::string& path);
    
    // Path utilities
    std::string joinPath(const std::string& path1, const std::string& path2);
    std::string normalizePath(const std::string& path);
    std::string getAbsolutePath(const std::string& path);
    std::string getRelativePath(const std::string& path, const std::string& base);
    
    // Directory listing
    std::vector<std::string> listDirectory(const std::string& path);
    std::vector<std::string> listFiles(const std::string& path, const std::string& extension = "");
    std::vector<std::string> listDirectories(const std::string& path);
    
    // File content
    std::string readTextFile(const std::string& path);
    bool writeTextFile(const std::string& path, const std::string& content);
    std::vector<uint8_t> readBinaryFile(const std::string& path);
    bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data);
    
    // Disk space
    uint64_t getAvailableSpace(const std::string& path);
    uint64_t getTotalSpace(const std::string& path);
    
    // File hashing (for change detection)
    std::string calculateMD5(const std::string& path);
    std::string calculateSHA256(const std::string& path);
    
    // Human readable sizes
    std::string formatFileSize(uint64_t bytes);
    std::string formatTime(std::time_t time);
}