#!/bin/bash

# Network Setup Script for USB Bridge
# Configures WiFi and Ethernet interfaces

set -e

CONFIG_DIR="/etc/usb-bridge"
LOG_FILE="/data/logs/network.log"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Setup WiFi
setup_wifi() {
    local ssid="$1"
    local password="$2"
    
    log "Setting up WiFi connection to $ssid"
    
    # Create wpa_supplicant configuration
    wpa_passphrase "$ssid" "$password" > /etc/wpa_supplicant/wpa_supplicant.conf
    
    # Add network configuration
    cat >> /etc/wpa_supplicant/wpa_supplicant.conf << EOF
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=US
EOF

    # Restart networking
    systemctl restart wpa_supplicant
    systemctl restart networking
    
    # Wait for connection
    for i in {1..30}; do
        if iwgetid wlan0 >/dev/null 2>&1; then
            log "WiFi connected successfully"
            ip addr show wlan0 | grep 'inet ' | awk '{print $2}' | while read ip; do
                log "WiFi IP address: $ip"
            done
            return 0
        fi
        sleep 1
    done
    
    log "ERROR: WiFi connection failed"
    return 1
}

# Setup Ethernet with static IP
setup_ethernet_static() {
    local ip="$1"
    local netmask="$2"
    local gateway="$3"
    
    log "Setting up Ethernet with static IP: $ip"
    
    cat > /etc/dhcpcd.conf << EOF
# USB Bridge Ethernet Configuration
interface eth0
static ip_address=$ip/$netmask
static routers=$gateway
static domain_name_servers=8.8.8.8 8.8.4.4
EOF

    systemctl restart dhcpcd
    
    # Wait for interface
    for i in {1..15}; do
        if ip addr show eth0 | grep -q "$ip"; then
            log "Ethernet configured successfully with IP: $ip"
            return 0
        fi
        sleep 1
    done
    
    log "ERROR: Ethernet configuration failed"
    return 1
}

# Enable DHCP on Ethernet
setup_ethernet_dhcp() {
    log "Setting up Ethernet with DHCP"
    
    cat > /etc/dhcpcd.conf << EOF
# USB Bridge Ethernet Configuration (DHCP)
interface eth0
# Let DHCP handle configuration
EOF

    systemctl restart dhcpcd
    
    # Wait for DHCP
    for i in {1..30}; do
        if ip addr show eth0 | grep 'inet ' >/dev/null; then
            log "Ethernet DHCP configured successfully"
            ip addr show eth0 | grep 'inet ' | awk '{print $2}' | while read ip; do
                log "Ethernet IP address: $ip"
            done
            return 0
        fi
        sleep 1
    done
    
    log "ERROR: Ethernet DHCP configuration failed"
    return 1
}

# Main function
main() {
    log "Network setup script started"
    
    case "$1" in
        "wifi")
            if [[ $# -eq 3 ]]; then
                setup_wifi "$2" "$3"
            else
                log "Usage: $0 wifi <ssid> <password>"
                exit 1
            fi
            ;;
        "ethernet-static")
            if [[ $# -eq 4 ]]; then
                setup_ethernet_static "$2" "$3" "$4"
            else
                log "Usage: $0 ethernet-static <ip> <netmask> <gateway>"
                exit 1
            fi
            ;;
        "ethernet-dhcp")
            setup_ethernet_dhcp
            ;;
        *)
            log "Usage: $0 {wifi|ethernet-static|ethernet-dhcp} [args...]"
            exit 1
            ;;
    esac
}

main "$@"
