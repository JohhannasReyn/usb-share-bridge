#pragma once

#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"
#include <memory>
#include <string>

namespace usb_bridge {

/**
 * Example implementations showing how clients interact with the new system
 */

// Example 1: SMB client writing a small file (uses buffer automatically)
class SmbClientExample {
public:
    void writeSmallFile(UsbBridge& bridge, const std::string& clientId) {
        // Client uploads file to temporary location first
        std::string tempFile = "/tmp/uploaded_file.txt";
        uint64_t fileSize = 1024 * 1024; // 1MB
        
        Logger::info("SMB client uploading small file...");
        
        // Queue the write operation
        uint64_t opId = bridge.clientWriteFile(
            clientId,
            ClientType::NETWORK_SMB,
            tempFile,
            "/mnt/usbdrive/documents/file.txt",
            fileSize,
            [clientId](const FileOperation& op) {
                // Callback when operation completes
                if (op.status == OperationStatus::COMPLETED) {
                    Logger::info("File written successfully for client " + clientId);
                } else if (op.status == OperationStatus::FAILED) {
                    Logger::error("File write failed: " + op.errorMessage);
                }
            }
        );
        
        Logger::info("Write operation queued with ID: " + std::to_string(opId));
        
        // Client can periodically check status
        OperationStatus status = bridge.getOperationStatus(opId);
        Logger::info("Current status: " + std::to_string(static_cast<int>(status)));
    }
};

// Example 2: USB host writing a large file (requires direct access)
class UsbHostLargeFileExample {
public:
    void writeLargeFile(UsbBridge& bridge, const std::string& clientId) {
        // Client has a 6GB file to write (exceeds buffer threshold)
        std::string tempFile = "/tmp/large_video.mp4";
        uint64_t fileSize = 6ULL * 1024 * 1024 * 1024; // 6GB
        
        Logger::info("USB host attempting to write large file...");
        
        // First, try to queue the operation
        uint64_t opId = bridge.clientWriteFile(
            clientId,
            ClientType::USB_HOST_1,
            tempFile,
            "/mnt/usbdrive/videos/large_video.mp4",
            fileSize,
            [this, &bridge, clientId, opId](const FileOperation& op) {
                if (op.status == OperationStatus::DIRECT_ACCESS_REQUIRED) {
                    Logger::info("Large file requires direct access");
                    this->handleDirectAccessRequired(bridge, clientId, opId);
                } else if (op.status == OperationStatus::COMPLETED) {
                    Logger::info("Large file written successfully");
                }
            }
        );
        
        Logger::info("Large write operation queued with ID: " + std::to_string(opId));
    }
    
private:
    void handleDirectAccessRequired(UsbBridge& bridge, 
                                    const std::string& clientId,
                                    uint64_t operationId) {
        Logger::info("Requesting direct access for large file operation...");
        
        // Request direct access with 30 second timeout
        bool granted = bridge.requestDirectAccess(
            clientId, 
            ClientType::USB_HOST_1, 
            operationId,
            std::chrono::seconds(30)
        );
        
        if (granted) {
            Logger::info("Direct access granted - client can now write directly");
            
            // At this point, the USB mass storage gadget is enabled
            // Client computer sees the drive and can write directly
            
            // Simulate direct write operation
            performDirectWrite();
            
            // When done, release access
            bridge.releaseDirectAccess(clientId);
            Logger::info("Direct access released - board resumed control");
            
        } else {
            Logger::error("Failed to obtain direct access");
        }
    }
    
    void performDirectWrite() {
        // Client would write directly to the mounted drive
        Logger::info("Performing direct write operation...");
        // ... actual file write happens here ...
        Logger::info("Direct write completed");
    }
};

// Example 3: HTTP client reading multiple files
class HttpClientBatchRead {
public:
    void readMultipleFiles(UsbBridge& bridge, const std::string& clientId) {
        std::vector<std::string> filesToRead = {
            "/mnt/usbdrive/photos/img1.jpg",
            "/mnt/usbdrive/photos/img2.jpg",
            "/mnt/usbdrive/photos/img3.jpg"
        };
        
        std::vector<uint64_t> operationIds;
        
        Logger::info("HTTP client requesting batch read of " + 
                    std::to_string(filesToRead.size()) + " files");
        
        // Queue all read operations
        for (const auto& filePath : filesToRead) {
            uint64_t opId = bridge.clientReadFile(
                clientId,
                ClientType::NETWORK_HTTP,
                filePath,
                [clientId, filePath](const FileOperation& op) {
                    if (op.status == OperationStatus::COMPLETED) {
                        Logger::info("File ready for download: " + filePath);
                        Logger::info("Buffer location: " + op.localBufferPath);
                        // Client can now download from buffer location
                    }
                }
            );
            
            operationIds.push_back(opId);
        }
        
        // Wait for all operations to complete
        Logger::info("Waiting for all read operations to complete...");
        
        bool allCompleted = false;
        while (!allCompleted) {
            allCompleted = true;
            for (uint64_t opId : operationIds) {
                OperationStatus status = bridge.getOperationStatus(opId);
                if (status != OperationStatus::COMPLETED && 
                    status != OperationStatus::FAILED) {
                    allCompleted = false;
                    break;
                }
            }
            
            if (!allCompleted) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        Logger::info("All read operations completed");
    }
};

// Example 4: System monitoring - checking queue and buffer status
class SystemMonitorExample {
public:
    void monitorSystem(UsbBridge& bridge) {
        Logger::info("=== System Status ===");
        
        // Get overall system status
        auto status = bridge.getStatus();
        
        Logger::info("Access Mode: " + 
                    std::string(status.currentAccessMode == AccessMode::BOARD_MANAGED ? 
                               "BOARD_MANAGED" : "DIRECT_ACCESS"));
        Logger::info("Access Holder: " + status.accessHolder);
        Logger::info("Queued Operations: " + std::to_string(status.queuedOperations));
        Logger::info("Buffer Usage: " + 
                    std::to_string(status.usedBufferSpace / (1024*1024)) + " MB / " +
                    std::to_string((status.usedBufferSpace + status.availableBufferSpace) / (1024*1024)) + " MB");
        Logger::info("Drive: " + (status.driveConnected ? "Connected" : "Not Connected"));
        
        if (status.driveConnected) {
            Logger::info("Drive Usage: " + 
                        std::to_string(status.driveUsed / (1024*1024*1024)) + " GB / " +
                        std::to_string(status.driveCapacity / (1024*1024*1024)) + " GB");
        }
        
        // Get queued operations
        auto queuedOps = bridge.getQueuedOperations();
        Logger::info("\n=== Queued Operations ===");
        for (const auto& op : queuedOps) {
            std::string opType;
            switch (op->type) {
                case OperationType::READ: opType = "READ"; break;
                case OperationType::WRITE: opType = "WRITE"; break;
                case OperationType::DELETE: opType = "DELETE"; break;
                case OperationType::MKDIR: opType = "MKDIR"; break;
                case OperationType::MOVE: opType = "MOVE"; break;
            }
            
            Logger::info("Op #" + std::to_string(op->id) + ": " + 
                        opType + " - Client: " + op->clientId + 
                        " - Size: " + std::to_string(op->fileSize / (1024*1024)) + " MB");
        }
        
        // Get statistics
        auto stats = bridge.getOperationQueue().getStatistics();
        Logger::info("\n=== Operation Statistics ===");
        Logger::info("Total Operations: " + std::to_string(stats.totalOperations));
        Logger::info("Completed: " + std::to_string(stats.completedOperations));
        Logger::info("Failed: " + std::to_string(stats.failedOperations));
        Logger::info("Required Direct Access: " + std::to_string(stats.directAccessOperations));
        Logger::info("Bytes Read: " + std::to_string(stats.bytesRead / (1024*1024)) + " MB");
        Logger::info("Bytes Written: " + std::to_string(stats.bytesWritten / (1024*1024)) + " MB");
        Logger::info("Avg Operation Time: " + std::to_string(stats.averageOperationTime) + " ms");
    }
};

// Example 5: Graceful handling of client disconnect
class ClientDisconnectExample {
public:
    void handleClientDisconnect(UsbBridge& bridge, const std::string& clientId) {
        Logger::info("Client " + clientId + " disconnecting...");
        
        // Get all pending operations for this client
        auto clientOps = bridge.getClientOperations(clientId);
        
        Logger::info("Client has " + std::to_string(clientOps.size()) + " pending operations");
        
        // Cancel all pending operations
        for (const auto& op : clientOps) {
            if (op->status == OperationStatus::QUEUED) {
                Logger::info("Cancelling operation #" + std::to_string(op->id));
                bridge.cancelOperation(op->id);
            }
        }
        
        // If client had direct access, it will be automatically released
        // by the onClientDisconnected handler
        
        Logger::info("Client cleanup complete");
    }
};

} // namespace usb_bridge