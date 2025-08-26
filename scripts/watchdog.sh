#!/bin/bash

# System Watchdog Script for USB Bridge
# Monitors system health and restarts services if needed

set -e

LOG_FILE="/data/logs/watchdog.log"
PID_FILE="/var/run/usb-bridge-watchdog.pid"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Check if main USB bridge process is running
check_main_process() {
    if pgrep -f "usb-share-bridge" >/dev/null; then
        return 0
    else
        log "WARNING: Main USB bridge process not running"
        return 1
    fi
}

# Check if SMB service is running
check_smb_service() {
    if systemctl is-active --quiet smbd; then
        return 0
    else
        log "WARNING: SMB service not running"
        return 1
    fi
}

# Check if HTTP service is running
check_http_service() {
    if systemctl is-active --quiet usb-bridge-http; then
        return 0
    else
        log "WARNING: HTTP service not running"
        return 1
    fi
}

# Check disk space
check_disk_space() {
    local usage=$(df /data | tail -1 | awk '{print $5}' | sed 's/%//')
    if [[ $usage -gt 90 ]]; then
        log "WARNING: Disk space usage is ${usage}%"
        # Clean old logs
        find /data/logs -name "*.log" -mtime +7 -delete
        return 1
    fi
    return 0
}

# Check system temperature (if sensors available)
check_temperature() {
    if command -v vcgencmd >/dev/null; then
        local temp=$(vcgencmd measure_temp | cut -d= -f2 | cut -d\' -f1)
        if (( $(echo "$temp > 80" | bc -l) )); then
            log "WARNING: High system temperature: ${temp}°C"
            return 1
        fi
    fi
    return 0
}

# Restart services if needed
restart_services() {
    local service="$1"
    
    case "$service" in
        "smb")
            log "Restarting SMB service"
            systemctl restart smbd nmbd
            ;;
        "http")
            log "Restarting HTTP service"
            systemctl restart usb-bridge-http
            ;;
        "main")
            log "Restarting main USB bridge process"
            systemctl restart usb-bridge
            ;;
    esac
}

# Main watchdog loop
watchdog_loop() {
    log "USB Bridge watchdog started (PID: $)"
    echo $ > "$PID_FILE"
    
    while true; do
        # Check various system components
        if ! check_main_process; then
            restart_services "main"
        fi
        
        if ! check_smb_service; then
            restart_services "smb"
        fi
        
        if ! check_http_service; then
            restart_services "http"
        fi
        
        check_disk_space
        check_temperature
        
        # Sleep for 60 seconds before next check
        sleep 60
    done
}

# Cleanup function
cleanup() {
    log "Watchdog shutting down"
    rm -f "$PID_FILE"
    exit 0
}

# Set up signal handlers
trap cleanup SIGTERM SIGINT

# Check if already running
if [[ -f "$PID_FILE" ]]; then
    old_pid=$(cat "$PID_FILE")
    if kill -0 "$old_pid" 2>/dev/null; then
        log "Watchdog already running (PID: $old_pid)"
        exit 1
    else
        rm -f "$PID_FILE"
    fi
fi

# Start watchdog
case "$1" in
    "start")
        watchdog_loop &
        ;;
    "stop")
        if [[ -f "$PID_FILE" ]]; then
            kill $(cat "$PID_FILE")
            rm -f "$PID_FILE"
            log "Watchdog stopped"
        else
            log "Watchdog not running"
        fi
        ;;
    "status")
        if [[ -f "$PID_FILE" ]] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
            log "Watchdog is running (PID: $(cat "$PID_FILE"))"
        else
            log "Watchdog is not running"
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|status}"
        exit 1
        ;;
esac