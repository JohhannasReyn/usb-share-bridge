#!/bin/bash

# USB Bridge NAS Services Startup Script
# For BigTechTree Pi V1.2.1

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="/etc/usb-bridge"
DATA_DIR="/data"
LOG_FILE="$DATA_DIR/logs/startup.log"

# Logging function
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

log "Starting USB Bridge NAS services..."

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   log "ERROR: This script must be run as root"
   exit 1
fi

# Create necessary directories
mkdir -p "$DATA_DIR/logs"
mkdir -p "$DATA_DIR/cache"
mkdir -p "/mnt/usb_bridge"

# Load configuration
if [[ -f "$CONFIG_DIR/network.json" ]]; then
    SMB_ENABLED=$(jq -r '.services.smb.enabled' "$CONFIG_DIR/network.json")
    HTTP_ENABLED=$(jq -r '.services.http.enabled' "$CONFIG_DIR/network.json")
    SMB_PORT=$(jq -r '.services.smb.port' "$CONFIG_DIR/network.json")
    HTTP_PORT=$(jq -r '.services.http.port' "$CONFIG_DIR/network.json")
else
    log "WARNING: Network configuration not found, using defaults"
    SMB_ENABLED=true
    HTTP_ENABLED=true
    SMB_PORT=445
    HTTP_PORT=8080
fi

# Start SMB/CIFS service
start_smb() {
    log "Starting SMB/CIFS service on port $SMB_PORT..."
    
    # Generate smb.conf
    cat > /etc/samba/smb.conf << EOF
[global]
    workgroup = WORKGROUP
    server string = USB Bridge NAS
    netbios name = USB-BRIDGE
    security = user
    map to guest = bad user
    dns proxy = no
    
    # Performance optimizations
    socket options = TCP_NODELAY SO_RCVBUF=8192 SO_SNDBUF=8192
    read raw = yes
    write raw = yes
    max xmit = 65535
    dead time = 15
    getwd cache = yes

[USB_SHARE]
    comment = USB Bridge Shared Storage
    path = /mnt/usb_bridge
    browseable = yes
    read only = no
    guest ok = yes
    create mask = 0755
    directory mask = 0755
    force user = nobody
    force group = nogroup
EOF

    # Start Samba services
    systemctl enable smbd nmbd
    systemctl restart smbd nmbd
    
    if systemctl is-active --quiet smbd; then
        log "SMB service started successfully"
    else
        log "ERROR: Failed to start SMB service"
        return 1
    fi
}

# Start HTTP service
start_http() {
    log "Starting HTTP service on port $HTTP_PORT..."
    
    # Simple Python HTTP server for file access
    cat > /usr/local/bin/usb-bridge-http.py << 'EOF'
#!/usr/bin/env python3
import http.server
import socketserver
import os
import json
import urllib.parse
from pathlib import Path

class USBBridgeHTTPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory="/mnt/usb_bridge", **kwargs)
    
    def do_GET(self):
        if self.path.startswith('/api/'):
            self.handle_api_request()
        else:
            super().do_GET()
    
    def handle_api_request(self):
        if self.path == '/api/status':
            self.send_json_response({
                'status': 'online',
                'mount_point': '/mnt/usb_bridge',
                'available': os.path.ismount('/mnt/usb_bridge')
            })
        elif self.path == '/api/files':
            path = self.headers.get('X-Path', '/')
            files = self.list_files(path)
            self.send_json_response({'files': files})
        else:
            self.send_json_response({'error': 'Not found'}, 404)
    
    def send_json_response(self, data, status=200):
        self.send_response(status)
        self.send_header('Content-type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())
    
    def list_files(self, path):
        full_path = os.path.join('/mnt/usb_bridge', path.lstrip('/'))
        if not os.path.exists(full_path):
            return []
        
        files = []
        for item in os.listdir(full_path):
            item_path = os.path.join(full_path, item)
            stat = os.stat(item_path)
            files.append({
                'name': item,
                'size': stat.st_size if os.path.isfile(item_path) else 0,
                'is_directory': os.path.isdir(item_path),
                'modified': stat.st_mtime
            })
        return files

if __name__ == "__main__":
    PORT = int(os.environ.get('HTTP_PORT', 8080))
    with socketserver.TCPServer(("", PORT), USBBridgeHTTPHandler) as httpd:
        print(f"HTTP server started on port {PORT}")
        httpd.serve_forever()
EOF

    chmod +x /usr/local/bin/usb-bridge-http.py
    
    # Create systemd service
    cat > /etc/systemd/system/usb-bridge-http.service << EOF
[Unit]
Description=USB Bridge HTTP Service
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
Environment=HTTP_PORT=$HTTP_PORT
ExecStart=/usr/local/bin/usb-bridge-http.py
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

    systemctl enable usb-bridge-http
    systemctl restart usb-bridge-http
    
    if systemctl is-active --quiet usb-bridge-http; then
        log "HTTP service started successfully"
    else
        log "ERROR: Failed to start HTTP service"
        return 1
    fi
}

# Main execution
main() {
    log "USB Bridge NAS startup initiated"
    
    # Check if USB drive is mounted
    if ! mountpoint -q /mnt/usb_bridge; then
        log "WARNING: No USB drive mounted at /mnt/usb_bridge"
        log "Services will start but won't be functional until drive is connected"
    fi
    
    # Start services based on configuration
    if [[ "$SMB_ENABLED" == "true" ]]; then
        start_smb || log "SMB service failed to start"
    else
        log "SMB service disabled in configuration"
    fi
    
    if [[ "$HTTP_ENABLED" == "true" ]]; then
        start_http || log "HTTP service failed to start"
    else
        log "HTTP service disabled in configuration"
    fi
    
    log "USB Bridge NAS startup completed"
}

# Run main function
main "$@"
