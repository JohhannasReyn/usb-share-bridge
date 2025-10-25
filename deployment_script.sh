#!/bin/bash

# USB Bridge Deployment Script
# Usage: ./deploy.sh <board-ip> [build|deploy|full]

set -e

BOARD_IP="$1"
ACTION="${2:-full}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_USER="pi"
BOARD_HOME="/home/$BOARD_USER"
PROJECT_NAME="usb-share-bridge"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
    exit 1
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

check_board_connection() {
    log "Checking connection to board at $BOARD_IP..."
    if ! ping -c 1 -W 5 "$BOARD_IP" >/dev/null 2>&1; then
        error "Cannot reach board at $BOARD_IP"
    fi
    
    if ! ssh -o ConnectTimeout=10 "$BOARD_USER@$BOARD_IP" "echo 'Connected'" >/dev/null 2>&1; then
        error "Cannot SSH to board. Check SSH key setup or use: ssh-copy-id $BOARD_USER@$BOARD_IP"
    fi
    
    success "Board connection verified"
}

install_dependencies() {
    log "Installing dependencies on board..."
    
    ssh "$BOARD_USER@$BOARD_IP" << 'EOF'
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
  libsamba-dev libpigpio-dev libwiringpi-dev \
  nlohmann-json3-dev samba samba-common-bin python3-dev \
  libssl-dev libi2c-dev libfreetype6-dev

# Enable required kernel modules
sudo modprobe dwc2
sudo modprobe libcomposite

# Add to boot modules
echo "dwc2" | sudo tee -a /etc/modules
echo "libcomposite" | sudo tee -a /etc/modules

# Enable pigpio daemon
sudo systemctl enable pigpiod
sudo systemctl start pigpiod

# Create required directories
sudo mkdir -p /etc/usb-bridge
sudo mkdir -p /data/logs
sudo mkdir -p /data/cache
sudo mkdir -p /mnt/usb_bridge
sudo mkdir -p /web

# Set permissions
sudo chown -R pi:pi /data
sudo chmod 755 /mnt/usb_bridge
EOF

    success "Dependencies installed"
}

deploy_source() {
    log "Deploying source code to board..."
    
    # Create remote directory
    ssh "$BOARD_USER@$BOARD_IP" "mkdir -p $BOARD_HOME/$PROJECT_NAME"
    
    # Sync source code (excluding build artifacts)
    rsync -av --progress --delete \
      --exclude='.git' \
      --exclude='build*' \
      --exclude='*.o' \
      --exclude='*.so' \
      --exclude='.vscode' \
      --exclude='*.log' \
      "$PROJECT_DIR/" "$BOARD_USER@$BOARD_IP:$BOARD_HOME/$PROJECT_NAME/"
    
    success "Source code deployed"
}

build_project() {
    log "Building project on board..."
    
    ssh "$BOARD_USER@$BOARD_IP" << EOF
cd $BOARD_HOME/$PROJECT_NAME

# Create build directory
mkdir -p build
cd build

# Configure build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..

# Build project
make -j\$(nproc)

echo "Build completed successfully"
EOF

    success "Project built successfully"
}

install_project() {
    log "Installing project on board..."
    
    ssh "$BOARD_USER@$BOARD_IP" << EOF
cd $BOARD_HOME/$PROJECT_NAME/build

# Install binaries and files
sudo make install

# Copy configuration files
sudo cp -r ../config/* /etc/usb-bridge/ 2>/dev/null || true

# Copy web interface
sudo cp -r ../web/* /web/ 2>/dev/null || true

# Make scripts executable
sudo chmod +x /usr/local/bin/*.sh 2>/dev/null || true

# Create systemd service
sudo tee /etc/systemd/system/usb-bridge.service > /dev/null << 'EOL'
[Unit]
Description=USB Bridge Service
After=network.target
Wants=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/usb-share-bridge
Restart=always
RestartSec=3
Environment=DISPLAY=:0

[Install]
WantedBy=multi-user.target
EOL

# Reload systemd and enable service
sudo systemctl daemon-reload
sudo systemctl enable usb-bridge

echo "Project installed successfully"
EOF

    success "Project installed"
}

start_service() {
    log "Starting USB Bridge service..."
    
    ssh "$BOARD_USER@$BOARD_IP" << 'EOF'
# Stop service if running
sudo systemctl stop usb-bridge 2>/dev/null || true

# Start service
sudo systemctl start usb-bridge

# Check status
sleep 2
if sudo systemctl is-active --quiet usb-bridge; then
    echo "Service started successfully"
    sudo systemctl status usb-bridge --no-pager -l
else
    echo "Service failed to start"
    sudo journalctl -u usb-bridge --no-pager -l
    exit 1
fi
EOF

    success "USB Bridge service started"
}

setup_development() {
    log "Setting up development environment..."
    
    # Setup VS Code remote development
    if command -v code >/dev/null 2>&1; then
        log "Setting up VS Code remote development..."
        
        # Create VS Code settings for remote development
        mkdir -p .vscode
        
        cat > .vscode/settings.json << 'EOF'
{
    "remote.SSH.defaultExtensions": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "ms-python.python"
    ],
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "cmake.buildDirectory": "${workspaceFolder}/build"
}
EOF

        success "VS Code remote development configured"
    fi
    
    # Setup convenient aliases
    ssh "$BOARD_USER@$BOARD_IP" << 'EOF'
# Add useful aliases to .bashrc
cat >> ~/.bashrc << 'EOL'

# USB Bridge aliases
alias bridge-status='sudo systemctl status usb-bridge'
alias bridge-logs='sudo journalctl -u usb-bridge -f'
alias bridge-restart='sudo systemctl restart usb-bridge'
alias bridge-stop='sudo systemctl stop usb-bridge'
alias bridge-start='sudo systemctl start usb-bridge'
alias bridge-build='cd ~/usb-share-bridge/build && make -j$(nproc)'
alias bridge-install='cd ~/usb-share-bridge/build && sudo make install && sudo systemctl restart usb-bridge'
EOL

source ~/.bashrc
EOF

    success "Development environment setup complete"
}

show_status() {
    log "Checking USB Bridge status..."
    
    ssh "$BOARD_USER@$BOARD_IP" << 'EOF'
echo "=== System Status ==="
echo "Uptime: $(uptime)"
echo "Memory: $(free -h | grep Mem:)"
echo

echo "=== USB Bridge Service ==="
sudo systemctl status usb-bridge --no-pager -l || true
echo

echo "=== Network Interfaces ==="
ip addr show | grep -E "(inet|UP|DOWN)" || true
echo

echo "=== USB Gadget Status ==="
ls -la /sys/kernel/config/usb_gadget/ 2>/dev/null || echo "No USB gadgets configured"
echo

echo "=== Mount Points ==="
df -h | grep -E "(mnt|Filesystem)" || true
echo

echo "=== Recent Logs ==="
sudo journalctl -u usb-bridge --no-pager -l --since "5 minutes ago" || true
EOF
}

# Main execution
case "$ACTION" in
    "deps")
        check_board_connection
        install_dependencies
        ;;
    "deploy")
        check_board_connection
        deploy_source
        ;;
    "build")
        check_board_connection
        build_project
        ;;
    "install")
        check_board_connection
        install_project
        ;;
    "start")
        check_board_connection
        start_service
        ;;
    "status")
        check_board_connection
        show_status
        ;;
    "dev")
        check_board_connection
        setup_development
        ;;
    "full")
        check_board_connection
        install_dependencies
        deploy_source
        build_project
        install_project
        start_service
        setup_development
        show_status
        ;;
    *)
        echo "Usage: $0 <board-ip> [deps|deploy|build|install|start|status|dev|full]"
        echo
        echo "Commands:"
        echo "  deps    - Install dependencies on board"
        echo "  deploy  - Deploy source code to board"
        echo "  build   - Build project on board"
        echo "  install - Install built project"
        echo "  start   - Start the USB Bridge service"
        echo "  status  - Show system and service status"
        echo "  dev     - Setup development environment"
        echo "  full    - Run complete deployment (default)"
        echo
        echo "Examples:"
        echo "  $0 192.168.1.100           # Full deployment"
        echo "  $0 192.168.1.100 build     # Just build"
        echo "  $0 192.168.1.100 status    # Check status"
        exit 1
        ;;
esac

success "Deployment completed successfully!"

echo
echo "=== Next Steps ==="
echo "1. Check service status: ssh $BOARD_USER@$BOARD_IP 'bridge-status'"
echo "2. View logs: ssh $BOARD_USER@$BOARD_IP 'bridge-logs'"
echo "3. Access web interface: http://$BOARD_IP:8080"
echo "4. For development: code --remote ssh-remote+$BOARD_USER@$BOARD_IP $BOARD_HOME/$PROJECT_NAME"