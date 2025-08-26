#!/bin/bash

# USB Drive Mounting Script for USB Bridge
# Handles automatic detection and mounting of USB storage devices

set -e

LOG_FILE="/data/logs/mount.log"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Find USB storage devices
find_usb_devices() {
    # Look for USB mass storage devices
    for device in /sys/block/sd*; do
        if [[ -e "$device" ]]; then
            device_name=$(basename "$device")
            # Check if it's a USB device
            if [[ -e "/sys/block/$device_name/device" ]]; then
                device_path=$(readlink -f "/sys/block/$device_name/device")
                if [[ "$device_path" == *"usb"* ]]; then
                    echo "/dev/${device_name}1"  # Assume first partition
                fi
            fi
        fi
    done
}

# Mount USB device
mount_device() {
    local device="$1"
    local mount_point="/mnt/usb_bridge"
    
    if [[ ! -e "$device" ]]; then
        log "ERROR: Device $device does not exist"
        return 1
    fi
    
    # Create mount point if it doesn't exist
    mkdir -p "$mount_point"
    
    # Unmount if already mounted
    if mountpoint -q "$mount_point"; then
        log "Unmounting existing mount at $mount_point"
        umount "$mount_point" || true
    fi
    
    # Detect filesystem type
    fs_type=$(blkid -o value -s TYPE "$device" 2>/dev/null || echo "auto")
    
    log "Mounting $device ($fs_type) to $mount_point"
    
    # Mount with appropriate options
    case "$fs_type" in
        "ntfs")
            mount -t ntfs-3g "$device" "$mount_point" -o rw,uid=1000,gid=1000,dmask=022,fmask=133
            ;;
        "vfat"|"fat32")
            mount -t vfat "$device" "$mount_point" -o rw,uid=1000,gid=1000,dmask=022,fmask=133
            ;;
        "exfat")
            mount -t exfat "$device" "$mount_point" -o rw,uid=1000,gid=1000
            ;;
        "ext4"|"ext3"|"ext2")
            mount -t "$fs_type" "$device" "$mount_point" -o rw
            ;;
        *)
            mount "$device" "$mount_point"
            ;;
    esac
    
    if mountpoint -q "$mount_point"; then
        log "Successfully mounted $device to $mount_point"
        
        # Set permissions
        chmod 755 "$mount_point"
        
        # Log drive information
        df -h "$mount_point" | tail -1 | while read -r line; do
            log "Drive info: $line"
        done
        
        return 0
    else
        log "ERROR: Failed to mount $device"
        return 1
    fi
}

# Main execution
main() {
    log "USB drive mounting script started"
    
    if [[ $# -eq 1 ]]; then
        # Specific device provided
        mount_device "$1"
    else
        # Auto-detect USB devices
        devices=($(find_usb_devices))
        
        if [[ ${#devices[@]} -eq 0 ]]; then
            log "No USB storage devices found"
            exit 1
        fi
        
        log "Found ${#devices[@]} USB storage device(s): ${devices[*]}"
        
        # Mount the first available device
        for device in "${devices[@]}"; do
            if mount_device "$device"; then
                break
            fi
        done
    fi
}

main "$@"
