# Migration Guide: v1.x to v2.0 (Board-Managed Architecture)

## Overview

Version 2.0 introduces a fundamental architectural change from **client-managed direct access** to **board-managed queue-based operations**. This guide helps you migrate existing deployments.

## Breaking Changes

### 1. Access Model Changed

**v1.x (Old):**
```cpp
// Client directly requests drive access
mutexLocker.requestAccess(clientId);
// Perform operations directly
fs::copy("local.txt", "/mnt/usbdrive/file.txt");
mutexLocker.releaseAccess(clientId);
```

**v2.0 (New):**
```cpp
// Client queues operation, board handles it
uint64_t opId = bridge.clientWriteFile(
    clientId, 
    ClientType::NETWORK_SMB,
    "local.txt",
    "/mnt/usbdrive/file.txt", 
    fileSize
);

// Check status asynchronously
if (bridge.getOperationStatus(opId) == OperationStatus::COMPLETED) {
    // Operation done
}
```

### 2. MutexLocker API Changed

**v1.x AccessMode:**
- `NONE`
- `USB_HOST_1`
- `USB_HOST_2`
- `NETWORK`

**v2.0 AccessMode:**
- `NONE`
- `BOARD_MANAGED` ← **New default mode**
- `DIRECT_USB` ← Only for large files
- `DIRECT_NETWORK` ← Only for large files

### 3. New Components

**Required additions:**
- `FileOperationQueue`: Manages all file operations
- Local buffer system at `/data/buffer`
- New configuration parameters in `system.json`

## Migration Steps

### Step 1: Backup Current System

```bash
# Backup configuration
sudo cp -r /etc/usb-bridge /etc/usb-bridge.backup

# Backup data and logs
sudo cp -r /data /data.backup

# Note current settings
sudo systemctl status usb-bridge
```

### Step 2: Update System Configuration

Create or update `/etc/usb-bridge/system.json`:

```json
{
  "system": {
    "version": "2.0.0",
    "mode": "board_managed"
  },
  "buffer": {
    "path": "/data/buffer",
    "maxSize": 10737418240,
    "largeFileThreshold": 5368709120
  },
  "queue": {
    "enabled": true,
    "maxQueueSize": 1000,
    "operationCleanupAge": 86400
  },
  "access": {
    "defaultMode": "board_managed",
    "directAccessTimeout": 300,
    "maxDirectAccessDuration": 1800
  }
}
```

### Step 3: Create Buffer Directory

```bash
# Create buffer directory
sudo mkdir -p /data/buffer

# Set permissions
sudo chown biqu:biqu /data/buffer
sudo chmod 755 /data/buffer

# Verify space available
df -h /data
```

**Recommended:** Allocate at least 10GB for the buffer. Adjust based on your typical file sizes.

### Step 4: Update Source Code

#### 4.1 Update Network Server Code (SMB/HTTP)

**Old SMB write handler:**
```cpp
void handleWrite(const std::string& clientId, const std::string& path) {
    // Old: Request direct access
    if (mutexLocker.requestAccess(clientId, AccessMode::NETWORK)) {
        // Write directly
        std::ofstream file(path);
        // ... write data ...
        mutexLocker.releaseAccess(clientId);
    }
}
```

**New SMB write handler:**
```cpp
void handleWrite(const std::string& clientId, const std::string& path) {
    // New: Queue operation
    // First, receive file to temp location
    std::string tempPath = "/tmp/smb_upload_" + generateId();
    receiveFileToTemp(tempPath);
    
    uint64_t fileSize = fs::file_size(tempPath);
    
    // Queue write operation
    uint64_t opId = bridge.clientWriteFile(
        clientId,
        ClientType::NETWORK_SMB,
        tempPath,
        path,
        fileSize,
        [tempPath](const FileOperation& op) {
            // Cleanup temp file after operation
            fs::remove(tempPath);
            
            if (op.status == OperationStatus::DIRECT_ACCESS_REQUIRED) {
                // Handle large file - see Step 4.2
            }
        }
    );
    
    // Return operation ID to client for status tracking
    return opId;
}
```

#### 4.2 Add Direct Access Handler for Large Files

```cpp
void handleDirectAccessRequired(UsbBridge& bridge, 
                                const std::string& clientId,
                                uint64_t operationId,
                                ClientType clientType) {
    Logger::info("Operation requires direct access");
    
    // Request direct access
    bool granted = bridge.requestDirectAccess(
        clientId,
        clientType,
        operationId,
        std::chrono::seconds(30)
    );
    
    if (granted) {
        // For USB clients: USB gadget mode is now enabled
        // For network clients: Drive is accessible via SMB/HTTP
        
        // Notify client to proceed with direct operation
        notifyClientDirectAccessGranted(clientId);
        
        // Wait for client to complete operation
        waitForClientCompletion(clientId);
        
        // Release access
        bridge.releaseDirectAccess(clientId);
    } else {
        notifyClientDirectAccessDenied(clientId);
    }
}
```

#### 4.3 Update USB Host Controller

**Old USB host handler:**
```cpp
void onHostConnected(int hostId) {
    // Old: Immediately grant access
    std::string clientId = "usb_host_" + std::to_string(hostId);
    mutexLocker.requestAccess(clientId, 
        hostId == 0 ? AccessMode::USB_HOST_1 : AccessMode::USB_HOST_2);
    
    // Enable USB gadget mode
    enableUsbGadget();
}
```

**New USB host handler:**
```cpp
void onHostConnected(int hostId) {
    // New: Just register the host
    std::string clientId = "usb_host_" + std::to_string(hostId);
    
    // Host will use queue for normal operations
    // USB gadget only enabled when direct access needed
    
    Logger::info("USB host " + std::to_string(hostId) + " connected");
    
    // Optionally notify host about available operations
    notifyHostConnected(clientId);
}
```

### Step 5: Update Build System

Update `CMakeLists.txt` to include new files:

```cmake
# Add new source files
set(CORE_SOURCES
    src/core/UsbBridge.cpp
    src/core/StorageManager.cpp
    src/core/HostController.cpp
    src/core/MutexLocker.cpp
    src/core/FileChangeLogger.cpp
    src/core/ConfigManager.cpp
    src/core/FileOperationQueue.cpp  # New
)
```

### Step 6: Build and Install

```bash
cd ~/usb-share-bridge
rm -rf build
mkdir build && cd build

# Configure build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# Install
sudo make install

# Restart service
sudo systemctl restart usb-bridge
```

### Step 7: Verify Migration

```bash
# Check service status
sudo systemctl status usb-bridge

# Check logs for successful initialization
sudo journalctl -u usb-bridge -n 100

# Verify buffer directory created
ls -la /data/buffer

# Check configuration loaded
sudo journalctl -u usb-bridge | grep "Buffer configuration"
```

### Step 8: Test Operations

#### Test 1: Small File Write
```bash
# Via SMB (from another computer)
echo "test" > /mnt/smb_share/test.txt

# Check logs
sudo journalctl -u usb-bridge | grep "WRITE.*test.txt"
```

#### Test 2: Large File Write
```bash
# Create 6GB test file
dd if=/dev/zero of=/tmp/large.bin bs=1M count=6144

# Copy to SMB share
cp /tmp/large.bin /mnt/smb_share/

# Should see direct access request in logs
sudo journalctl -u usb-bridge | grep "Direct access"
```

#### Test 3: Queue Status
Check via HTTP API:
```bash
curl http://localhost:8080/api/status
curl http://localhost:8080/api/queue
```

## Rollback Procedure

If you need to rollback to v1.x:

```bash
# Stop service
sudo systemctl stop usb-bridge

# Restore backup configuration
sudo rm -rf /etc/usb-bridge
sudo mv /etc/usb-bridge.backup /etc/usb-bridge

# Restore old binary (if you kept it)
sudo cp /usr/local/bin/usb-share-bridge.v1 /usr/local/bin/usb-share-bridge

# Restart service
sudo systemctl start usb-bridge
```

## Common Issues and Solutions

### Issue 1: Buffer Directory Not Found

**Error:** `Failed to create buffer directory`

**Solution:**
```bash
sudo mkdir -p /data/buffer
sudo chown biqu:biqu /data/buffer
sudo chmod 755 /data/buffer
```

### Issue 2: Insufficient Buffer Space

**Error:** `Operation failed: insufficient buffer space`

**Solutions:**
1. Increase buffer size in config:
```json
{
  "buffer": {
    "maxSize": 21474836480  // 20GB
  }
}
```

2. Or decrease large file threshold:
```json
{
  "buffer": {
    "largeFileThreshold": 2147483648  // 2GB
  }
}
```

### Issue 3: Operations Timing Out

**Error:** `Operation timed out`

**Solution:**
Check drive performance:
```bash
# Check drive health
sudo smartctl -a /dev/sda1

# Check mount status
df -h | grep usbdrive

# Check for I/O errors
dmesg | grep -i error
```

### Issue 4: Queue Backup

**Symptom:** Many operations stuck in QUEUED state

**Solution:**
```bash
# Check queue status via API
curl http://localhost:8080/api/queue

# Check logs for errors
sudo journalctl -u usb-bridge | grep -i error

# Restart service if needed
sudo systemctl restart usb-bridge
```

## Performance Comparison

### v1.x Performance (Direct Access Model)
- Small file write: 2-5 seconds (includes USB switching)
- 100 small files: 200-500 seconds
- Large file: Fast once access granted
- Multi-client: Frequent conflicts and delays

### v2.0 Performance (Queue Model)
- Small file write: 0.5-1 second (queued immediately)
- 100 small files: 50-100 seconds (processed efficiently)
- Large file: Same as v1.x (uses direct access)
- Multi-client: Smooth concurrent operation

## Configuration Tuning

### For Workload with Many Small Files
```json
{
  "buffer": {
    "maxSize": 21474836480,  // 20GB
    "largeFileThreshold": 10737418240  // 10GB
  },
  "queue": {
    "maxQueueSize": 5000
  }
}
```

### For Workload with Few Large Files
```json
{
  "buffer": {
    "maxSize": 2147483648,  // 2GB
    "largeFileThreshold": 1073741824  // 1GB
  },
  "access": {
    "directAccessTimeout": 600  // 10 minutes
  }
}
```

### For High Concurrency
```json
{
  "queue": {
    "maxQueueSize": 10000,
    "operationCleanupAge": 3600  // 1 hour
  }
}
```

## API Changes Reference

### MutexLocker API

| v1.x Method | v2.0 Equivalent | Notes |
|-------------|-----------------|-------|
| `requestAccess(clientId, mode)` | `requestDirectAccess(clientId, type, opId)` | Only for large files |
| `releaseAccess(clientId)` | `releaseDirectAccess(clientId)` | Same |
| `hasAccess(clientId)` | `hasDirectAccess(clientId)` | Same |
| N/A | `isBoardManaged()` | New - check if in normal mode |
| N/A | `isDriveAccessible()` | New - check if drive ready |

### New UsbBridge API

| Method | Description |
|--------|-------------|
| `clientReadFile()` | Queue a read operation |
| `clientWriteFile()` | Queue a write operation |
| `clientDeleteFile()` | Queue a delete operation |
| `clientCreateDirectory()` | Queue a mkdir operation |
| `clientMoveFile()` | Queue a move operation |
| `getOperationStatus()` | Check operation status |
| `getOperation()` | Get operation details |
| `cancelOperation()` | Cancel queued operation |

## Support and Feedback

For issues or questions about migration:
1. Check logs: `sudo journalctl -u usb-bridge -f`
2. Review configuration: `/etc/usb-bridge/system.json`
3. Monitor queue: `curl http://localhost:8080/api/queue`
4. Open issue on GitHub with logs and configuration

## Post-Migration Checklist

- [ ] Configuration file updated with buffer settings
- [ ] Buffer directory created and has sufficient space
- [ ] Source code updated to use queue-based API
- [ ] Build successful with no errors
- [ ] Service starts without errors
- [ ] Small file operations work correctly
- [ ] Large file operations trigger direct access
- [ ] Multi-client access works smoothly
- [ ] Performance improved for common workloads
- [ ] Monitoring and logging working
- [ ] Documentation updated for your deployment

## Next Steps

After successful migration:
1. Monitor system for 24-48 hours
2. Adjust buffer and queue settings based on usage
3. Update client applications if needed
4. Document any custom configurations
5. Train users on new behavior (if applicable)