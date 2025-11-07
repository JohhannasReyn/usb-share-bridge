# Quick Reference Guide - USB Share Bridge v2.0 API

## Core Concepts

**Board-Managed Mode**: Default state where the CB1 board maintains drive access and processes operations through a queue.

**Direct Access Mode**: Fallback mode for large files where a client gets exclusive drive access.

**Operation Queue**: Central queue that processes all file operations in the background.

**Local Buffer**: Temporary storage (default 10GB) for buffering file operations.

## Client API Quick Reference

### Queue a File Write

```cpp
#include "core/UsbBridge.hpp"

// Upload file to temporary location first
std::string tempFile = "/tmp/client_upload.dat";
uint64_t fileSize = getFileSize(tempFile);

// Queue the write operation
uint64_t opId = bridge.clientWriteFile(
    "client123",                      // Your client ID
    ClientType::NETWORK_SMB,          // Client type
    tempFile,                         // Source (local buffer)
    "/mnt/usbdrive/data/file.dat",   // Destination on drive
    fileSize,                         // File size in bytes
    [](const FileOperation& op) {     // Optional callback
        if (op.status == OperationStatus::COMPLETED) {
            std::cout << "Write completed!\n";
        } else if (op.status == OperationStatus::DIRECT_ACCESS_REQUIRED) {
            // Handle large file - see below
        }
    }
);

// Operation queued, returns immediately
std::cout << "Operation ID: " << opId << std::endl;
```

### Queue a File Read

```cpp
// Queue read operation
uint64_t opId = bridge.clientReadFile(
    "client123",
    ClientType::NETWORK_HTTP,
    "/mnt/usbdrive/data/document.pdf",
    [](const FileOperation& op) {
        if (op.status == OperationStatus::COMPLETED) {
            // File is now in local buffer
            std::cout << "Download from: " << op.localBufferPath << std::endl;
        }
    }
);
```

### Check Operation Status

```cpp
// Poll for status
OperationStatus status = bridge.getOperationStatus(opId);

switch (status) {
    case OperationStatus::QUEUED:
        std::cout << "Waiting in queue...\n";
        break;
    case OperationStatus::IN_PROGRESS:
        std::cout << "Processing...\n";
        break;
    case OperationStatus::COMPLETED:
        std::cout << "Done!\n";
        break;
    case OperationStatus::FAILED:
        std::cout << "Failed\n";
        break;
    case OperationStatus::DIRECT_ACCESS_REQUIRED:
        std::cout << "Need direct access\n";
        break;
}
```

### Get Detailed Operation Info

```cpp
auto op = bridge.getOperation(opId);
if (op) {
    std::cout << "Client: " << op->clientId << std::endl;
    std::cout << "Type: " << static_cast<int>(op->type) << std::endl;
    std::cout << "Size: " << op->fileSize << " bytes\n";
    std::cout << "Progress: " << op->bytesProcessed << " / " << op->fileSize << std::endl;
    
    if (op->status == OperationStatus::FAILED) {
        std::cout << "Error: " << op->errorMessage << std::endl;
    }
}
```

### Cancel an Operation

```cpp
if (bridge.cancelOperation(opId)) {
    std::cout << "Operation cancelled\n";
} else {
    std::cout << "Cannot cancel (already in progress or completed)\n";
}
```

### Handle Large Files (Direct Access)

```cpp
void handleLargeFile(UsbBridge& bridge, const std::string& clientId, 
                     uint64_t opId) {
    // Request direct access with 30 second timeout
    bool granted = bridge.requestDirectAccess(
        clientId,
        ClientType::USB_HOST_1,
        opId,
        std::chrono::seconds(30)
    );
    
    if (granted) {
        std::cout << "Direct access granted!\n";
        
        // For USB clients: Drive is now visible as USB mass storage
        // For network clients: Direct SMB/HTTP access available
        
        // Perform your operation directly
        performDirectFileOperation();
        
        // IMPORTANT: Release access when done
        bridge.releaseDirectAccess(clientId);
        
    } else {
        std::cout << "Failed to get direct access\n";
    }
}
```

### Using RAII for Direct Access

```cpp
#include "core/MutexLocker.hpp"

// Automatic cleanup with RAII
{
    DirectAccessGuard guard(
        bridge.getMutexLocker(),
        "client123",
        ClientType::USB_HOST_1,
        operationId,
        std::chrono::seconds(30)
    );
    
    if (guard.isAcquired()) {
        // Perform direct operation
        performDirectFileOperation();
        
        // Access automatically released when guard goes out of scope
    }
} // Guard destroyed, access released automatically
```

## Other Operations

### Delete File

```cpp
uint64_t opId = bridge.clientDeleteFile(
    "client123",
    ClientType::NETWORK_SMB,
    "/mnt/usbdrive/data/old_file.dat"
);
```

### Create Directory

```cpp
uint64_t opId = bridge.clientCreateDirectory(
    "client123",
    ClientType::NETWORK_SMB,
    "/mnt/usbdrive/data/new_folder"
);
```

### Move/Rename File

```cpp
uint64_t opId = bridge.clientMoveFile(
    "client123",
    ClientType::NETWORK_SMB,
    "/mnt/usbdrive/data/old_name.txt",
    "/mnt/usbdrive/data/new_name.txt"
);
```

## System Monitoring

### Get System Status

```cpp
auto status = bridge.getStatus();

std::cout << "Drive: " << (status.driveConnected ? "Connected" : "Disconnected") << std::endl;
std::cout << "Access Mode: " << static_cast<int>(status.currentAccessMode) << std::endl;
std::cout << "Access Holder: " << status.accessHolder << std::endl;
std::cout << "Queued Operations: " << status.queuedOperations << std::endl;
std::cout << "Buffer Used: " << (status.usedBufferSpace / (1024*1024)) << " MB\n";
std::cout << "Buffer Available: " << (status.availableBufferSpace / (1024*1024)) << " MB\n";
```

### Get Queue Statistics

```cpp
auto stats = bridge.getOperationQueue().getStatistics();

std::cout << "Total Operations: " << stats.totalOperations << std::endl;
std::cout << "Completed: " << stats.completedOperations << std::endl;
std::cout << "Failed: " << stats.failedOperations << std::endl;
std::cout << "Required Direct Access: " << stats.directAccessOperations << std::endl;
std::cout << "Bytes Read: " << (stats.bytesRead / (1024*1024)) << " MB\n";
std::cout << "Bytes Written: " << (stats.bytesWritten / (1024*1024)) << " MB\n";
std::cout << "Avg Time: " << stats.averageOperationTime << " ms\n";
```

### Get All Queued Operations

```cpp
auto queuedOps = bridge.getQueuedOperations();

std::cout << "Queued operations:\n";
for (const auto& op : queuedOps) {
    std::cout << "  #" << op->id << ": " 
              << op->clientId << " - "
              << (op->fileSize / (1024*1024)) << " MB\n";
}
```

### Get Client's Operations

```cpp
auto clientOps = bridge.getClientOperations("client123");

std::cout << "Client's operations:\n";
for (const auto& op : clientOps) {
    std::cout << "  #" << op->id << ": " 
              << statusToString(op->status) << std::endl;
}
```

## Configuration

### Load Configuration

```cpp
ConfigManager& config = bridge.getConfig();

// Get buffer settings
std::string bufferPath = config.getString("buffer.path");
uint64_t bufferSize = config.getUInt64("buffer.maxSize");
uint64_t largeFileThreshold = config.getUInt64("buffer.largeFileThreshold");

// Check if key exists
if (config.hasKey("queue.enabled")) {
    bool queueEnabled = config.getBool("queue.enabled");
}
```

### Typical Configuration Values

```cpp
// Default values
const std::string DEFAULT_BUFFER_PATH = "/data/buffer";
const uint64_t DEFAULT_BUFFER_SIZE = 10ULL * 1024 * 1024 * 1024; // 10GB
const uint64_t DEFAULT_LARGE_FILE_THRESHOLD = 5ULL * 1024 * 1024 * 1024; // 5GB
const uint64_t DEFAULT_MAX_QUEUE_SIZE = 1000;
const uint64_t DEFAULT_CLEANUP_AGE = 86400; // 24 hours in seconds
```

## Common Patterns

### Pattern 1: Simple Write with Callback

```cpp
void uploadFile(UsbBridge& bridge, const std::string& localPath, 
                const std::string& remotePath) {
    uint64_t size = getFileSize(localPath);
    
    bridge.clientWriteFile(
        "myClient",
        ClientType::NETWORK_HTTP,
        localPath,
        remotePath,
        size,
        [remotePath](const FileOperation& op) {
            if (op.status == OperationStatus::COMPLETED) {
                std::cout << "Uploaded: " << remotePath << std::endl;
            } else if (op.status == OperationStatus::FAILED) {
                std::cerr << "Upload failed: " << op.errorMessage << std::endl;
            }
        }
    );
}
```

### Pattern 2: Batch Upload with Progress

```cpp
void uploadBatch(UsbBridge& bridge, const std::vector<std::string>& files) {
    std::atomic<int> completed{0};
    int total = files.size();
    
    for (const auto& file : files) {
        bridge.clientWriteFile(
            "myClient",
            ClientType::NETWORK_SMB,
            file,
            "/mnt/usbdrive/" + getFilename(file),
            getFileSize(file),
            [&completed, total](const FileOperation& op) {
                if (op.status == OperationStatus::COMPLETED) {
                    completed++;
                    std::cout << "Progress: " << completed << "/" << total << std::endl;
                }
            }
        );
    }
}
```

### Pattern 3: Synchronous Wait for Completion

```cpp
bool writeAndWait(UsbBridge& bridge, const std::string& src, 
                  const std::string& dst, uint64_t size) {
    uint64_t opId = bridge.clientWriteFile("myClient", ClientType::NETWORK_SMB, 
                                           src, dst, size);
    
    // Poll until complete
    while (true) {
        OperationStatus status = bridge.getOperationStatus(opId);
        
        if (status == OperationStatus::COMPLETED) {
            return true;
        } else if (status == OperationStatus::FAILED) {
            return false;
        } else if (status == OperationStatus::DIRECT_ACCESS_REQUIRED) {
            // Handle direct access
            handleDirectAccess(bridge, opId);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### Pattern 4: Error Handling

```cpp
void robustWrite(UsbBridge& bridge, const std::string& file) {
    try {
        uint64_t size = getFileSize(file);
        
        // Check buffer space first
        if (size > bridge.getOperationQueue().getAvailableBufferSpace()) {
            std::cout << "File requires direct access\n";
            // Handle accordingly
        }
        
        uint64_t opId = bridge.clientWriteFile(
            "myClient", ClientType::NETWORK_SMB, 
            file, "/mnt/usbdrive/file.dat", size
        );
        
        // Monitor operation
        auto op = bridge.getOperation(opId);
        if (!op) {
            throw std::runtime_error("Operation not found");
        }
        
        // Wait for completion
        // ... polling code ...
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
```

## Client Types

```cpp
enum class ClientType {
    USB_HOST_1,      // USB Host Port 1
    USB_HOST_2,      // USB Host Port 2
    NETWORK_SMB,     // SMB/CIFS network client
    NETWORK_HTTP,    // HTTP/REST API client
    SYSTEM          // Internal system operations
};
```

## Operation Types

```cpp
enum class OperationType {
    READ,     // Read file from drive
    WRITE,    // Write file to drive
    DELETE,   // Delete file/directory
    MKDIR,    // Create directory
    MOVE      // Move/rename file
};
```

## Operation Status

```cpp
enum class OperationStatus {
    QUEUED,                  // Waiting to be processed
    IN_PROGRESS,            // Currently being processed
    COMPLETED,              // Successfully completed
    FAILED,                 // Failed with error
    DIRECT_ACCESS_REQUIRED  // File too large, needs direct access
};
```

## Access Modes

```cpp
enum class AccessMode {
    NONE,              // No access (error state)
    BOARD_MANAGED,     // Board controls drive (normal mode)
    DIRECT_USB,        // USB client has direct access
    DIRECT_NETWORK     // Network client has direct access
};
```

## Best Practices

1. **Always use callbacks** for long-running operations
2. **Check buffer space** before large operations
3. **Handle DIRECT_ACCESS_REQUIRED** status properly
4. **Release direct access** as soon as possible
5. **Use RAII guards** (DirectAccessGuard) for direct access
6. **Don't block** waiting for operations - use callbacks
7. **Monitor queue depth** to avoid overwhelming the system
8. **Clean up temporary files** after operations complete
9. **Log operations** for debugging and auditing
10. **Check return values** and handle errors gracefully

## Common Errors and Solutions

**Error**: "Operation not found"
- **Cause**: Querying stale operation ID
- **Solution**: Operation may have been cleaned up; check sooner

**Error**: "Insufficient buffer space"
- **Cause**: Buffer full
- **Solution**: Wait for operations to complete or request direct access

**Error**: "Direct access timeout"
- **Cause**: Another client holds access too long
- **Solution**: Increase timeout or check access holder

**Error**: "Queue full"
- **Cause**: Too many pending operations
- **Solution**: Increase maxQueueSize in config or throttle requests

**Error**: "Operation cancelled"
- **Cause**: Client disconnected or manual cancellation
- **Solution**: Retry or handle gracefully

## Performance Tips

1. **Batch small files** - Queue many operations at once
2. **Use appropriate client type** - Affects priority and handling
3. **Pre-allocate buffer** - Ensure sufficient space before peak usage
4. **Monitor statistics** - Identify bottlenecks
5. **Tune thresholds** - Adjust largeFileThreshold based on workload
6. **Async everything** - Use callbacks, don't block
7. **Cleanup promptly** - Release resources after operations

## Debugging

```cpp
// Enable verbose logging
Logger::setLevel(LogLevel::DEBUG);

// Monitor operation queue
auto queuedOps = bridge.getQueuedOperations();
std::cout << "Queue depth: " << queuedOps.size() << std::endl;

// Check access mode
AccessMode mode = bridge.getMutexLocker().getCurrentAccessMode();
std::string holder = bridge.getMutexLocker().getCurrentAccessHolder();

// Get buffer usage
uint64_t used = bridge.getOperationQueue().getUsedBufferSpace();
uint64_t available = bridge.getOperationQueue().getAvailableBufferSpace();

// View operation details
auto op = bridge.getOperation(opId);
if (op) {
    std::cout << "Status: " << static_cast<int>(op->status) << std::endl;
    std::cout << "Error: " << op->errorMessage << std::endl;
}
```

## Further Reading

- `ARCHITECTURE.md` - Detailed architecture documentation
- `MIGRATION.md` - Migration guide from v1.x
- `DIAGRAMS.md` - Visual architecture diagrams
- `examples/ClientExamples.hpp` - Complete example implementations