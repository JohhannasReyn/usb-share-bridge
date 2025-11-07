# USB Share Bridge - Board-Managed Caching Architecture

## Overview of Changes

This document describes the major architectural change from **client-managed direct access** to **board-managed caching** with passthrough only for large files.

## Previous Architecture (Direct Pass-Through)

**Old Flow:**
```
Client Request → MutexLocker grants exclusive access → Client directly reads/writes USB drive
```

**Problems:**
- Clients compete for exclusive USB access
- Drive unmount conflicts when switching between clients  
- No buffering or caching
- Higher latency for small operations
- Potential data corruption if client crashes during write

## New Architecture (Board-Managed with Caching)

**New Flow:**
```
Client Request → FileOperationQueue → CacheManager → USB Drive (via WriteQueueManager)
                                    ↓
                            (if file too large)
                                    ↓
                           MutexLocker → Direct Access
```

**Benefits:**
- ✅ Board maintains control of USB drive at all times
- ✅ Local caching improves read performance
- ✅ Write-behind caching reduces client wait times
- ✅ Automatic retry on failures
- ✅ Better data integrity
- ✅ Graceful fallback for large files
- ✅ No client access conflicts

## Key Components

### 1. CacheManager
**Purpose**: Manages local cache storage for frequently accessed files

**Location**: `include/core/CacheManager.hpp`, `src/core/CacheManager.cpp`

**Key Features:**
- LRU eviction for clean files
- Reference counting prevents eviction of active files
- Dirty file tracking for write-back operations
- Configurable cache size (default: 4GB)
- Thread-safe operations

### 2. FileOperationQueue
**Purpose**: Queues and processes all file operations from clients

**Location**: `include/core/FileOperationQueue.hpp`, `src/core/FileOperationQueue.cpp`

**Operation Types:**
- READ, WRITE, DELETE, MKDIR, MOVE

**Key Features:**
- Automatic buffer allocation
- Operation status tracking
- Determines when direct access is needed
- Sequential processing to prevent conflicts

### 3. WriteQueueManager
**Purpose**: Asynchronously writes cached files to USB drive

**Location**: `include/core/WriteQueueManager.hpp`, `src/core/WriteQueueManager.cpp`

**Key Features:**
- Priority queue for write operations
- Background write thread
- Progress tracking
- Automatic retry on failure (up to 3 attempts)
- Write verification

### 4. MutexLocker
**Purpose**: Controls access modes and grants direct access when necessary

**Location**: `include/core/MutexLocker.hpp`, `src/core/MutexLocker.cpp`

**Access Modes:**
- BOARD_MANAGED, DIRECT_USB, DIRECT_NETWORK

## Configuration

See `config/system.json` for cache, buffer, and write queue settings.

## Testing

Run unit tests and integration tests to verify functionality.

For full documentation, see IMPLEMENTATION_PLAN.md