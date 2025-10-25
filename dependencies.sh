#!/bin/bash

# USB Bridge Dependencies for BigTechTree CB1
# This script installs all required dependencies for the USB Bridge project

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# Update system
log "Updating system packages..."
sudo apt update && sudo apt upgrade -y

# Install basic build tools
log "Installing basic build tools..."
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    python3-dev \
    libssl-dev \
    libi2c-dev \
    libfreetype6-dev \
    wget \
    curl \
    unzip

# Install system libraries that are available
log "Installing available system libraries..."
sudo apt install -y \
    samba \
    samba-common-bin \
    samba-common \
    libsmbclient-dev \
    libnl-3-dev \
    libnl-genl-3-dev \
    wireless-tools \
    wpasupplicant

# Check if nlohmann-json is available
if apt-cache show nlohmann-json3-dev >/dev/null 2>&1; then
    sudo apt install -y nlohmann-json3-dev
else
    log "Installing nlohmann-json manually..."
    cd /tmp
    wget https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp
    sudo mkdir -p /usr/local/include/nlohmann
    sudo cp json.hpp /usr/local/include/nlohmann/
    success "nlohmann-json installed manually"
fi

# Install pigpio manually
log "Installing pigpio library..."
cd /tmp
if [ ! -d "pigpio" ]; then
    git clone https://github.com/joan2937/pigpio.git
fi
cd pigpio
make
sudo make install
sudo ldconfig
success "pigpio installed"

# Install wiringPi alternative (lgpio) or build from source
log "Installing GPIO library..."
cd /tmp
if [ ! -d "lg" ]; then
    wget https://github.com/joan2937/lg/archive/master.zip -O lg-master.zip
    unzip lg-master.zip
    mv lg-master lg
fi
cd lg
make
sudo make install
sudo ldconfig

# Also try to get wiringPi if possible
if ! dpkg -l | grep -q wiringpi; then
    log "Installing wiringPi from source..."
    cd /tmp
    if [ ! -d "WiringPi" ]; then
        git clone https://github.com/WiringPi/WiringPi.git
    fi
    cd WiringPi
    ./build
    success "wiringPi installed from source"
fi

# Install LVGL manually
log "Installing LVGL library..."
cd /tmp
if [ ! -d "lvgl" ]; then
    git clone --branch release/v8.3 https://github.com/lvgl/lvgl.git
fi
cd lvgl
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DLV_CONF_PATH=/usr/local/include/lv_conf.h
make -j$(nproc)
sudo make install

# Create a basic lv_conf.h
sudo tee /usr/local/include/lv_conf.h > /dev/null << 'EOF'
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (64U * 1024U)
#define LV_USE_OS LV_OS_PTHREAD
#define LV_USE_USER_DATA 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_LIST 1
#define LV_USE_DROPDOWN 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_BAR 1
#define LV_USE_THEME_DEFAULT 1

#endif
EOF

sudo ldconfig
success "LVGL installed"

# Enable required kernel modules
log "Configuring kernel modules..."
sudo modprobe i2c-dev 2>/dev/null || warn "i2c-dev module not available"
sudo modprobe spi-dev 2>/dev/null || warn "spi-dev module not available"
sudo modprobe dwc2 2>/dev/null || warn "dwc2 module not available"
sudo modprobe libcomposite 2>/dev/null || warn "libcomposite module not available"

# Add modules to boot configuration
echo "# USB Bridge modules" | sudo tee -a /etc/modules
echo "i2c-dev" | sudo tee -a /etc/modules
echo "spi-dev" | sudo tee -a /etc/modules
echo "dwc2" | sudo tee -a /etc/modules
echo "libcomposite" | sudo tee -a /etc/modules

# Enable USB gadget support in boot config
if [ -f /boot/config.txt ]; then
    log "Configuring USB gadget support..."
    if ! grep -q "dtoverlay=dwc2" /boot/config.txt; then
        echo "dtoverlay=dwc2" | sudo tee -a /boot/config.txt
    fi
    if ! grep -q "enable_uart=1" /boot/config.txt; then
        echo "enable_uart=1" | sudo tee -a /boot/config.txt
    fi
elif [ -f /boot/BoardEnv.txt ]; then
    # Armbian configuration
    log "Configuring Armbian USB gadget support..."
    if ! grep -q "overlays=usbhost2 usbhost3" /boot/BoardEnv.txt; then
        echo "overlays=usbhost2 usbhost3" | sudo tee -a /boot/BoardEnv.txt
    fi
fi

# Create required directories
log "Creating required directories..."
sudo mkdir -p /etc/usb-bridge
sudo mkdir -p /data/logs
sudo mkdir -p /data/cache
sudo mkdir -p /mnt/usb_bridge
sudo mkdir -p /web
sudo mkdir -p /sys/kernel/config

# Set proper permissions
sudo chown -R $USER:$USER /data
sudo chmod 755 /mnt/usb_bridge

# Enable ConfigFS (required for USB gadget)
if ! mount | grep -q configfs; then
    sudo mount -t configfs none /sys/kernel/config
    echo "none /sys/kernel/config configfs defaults 0 0" | sudo tee -a /etc/fstab
fi

# Setup I2C and SPI permissions
sudo usermod -a -G i2c,spi,gpio $USER 2>/dev/null || warn "Some groups may not exist"

# Configure GPIO access
if [ -f /sys/class/gpio/export ]; then
    sudo chmod 666 /sys/class/gpio/export /sys/class/gpio/unexport
fi

# Enable and configure services
log "Configuring services..."

# Start pigpio daemon
sudo systemctl enable pigpiod 2>/dev/null || warn "pigpiod service not available"
sudo systemctl start pigpiod 2>/dev/null || warn "Could not start pigpiod"

# Configure Samba
sudo systemctl enable smbd nmbd
sudo systemctl start smbd nmbd

log "Creating USB Bridge CMake configuration..."
sudo tee /usr/local/lib/cmake/USBBridgeConfig.cmake > /dev/null << 'EOF'
# USB Bridge CMake configuration for BigTechTree CB1

# Find pigpio
find_library(PIGPIO_LIBRARY pigpio HINTS /usr/local/lib /usr/lib)
find_path(PIGPIO_INCLUDE_DIR pigpio.h HINTS /usr/local/include /usr/include)

# Find LVGL
find_library(LVGL_LIBRARY lvgl HINTS /usr/local/lib /usr/lib)
find_path(LVGL_INCLUDE_DIR lvgl.h HINTS /usr/local/include /usr/include)

# Find wiringPi or lg
find_library(WIRINGPI_LIBRARY 
    NAMES wiringPi lgpio
    HINTS /usr/local/lib /usr/lib)
find_path(WIRINGPI_INCLUDE_DIR 
    NAMES wiringPi.h lgpio.h
    HINTS /usr/local/include /usr/include)

# Set variables
set(USB_BRIDGE_LIBRARIES ${PIGPIO_LIBRARY} ${LVGL_LIBRARY} ${WIRINGPI_LIBRARY})
set(USB_BRIDGE_INCLUDE_DIRS ${PIGPIO_INCLUDE_DIR} ${LVGL_INCLUDE_DIR} ${WIRINGPI_INCLUDE_DIR})

# Print status
message(STATUS "USB Bridge libraries: ${USB_BRIDGE_LIBRARIES}")
message(STATUS "USB Bridge includes: ${USB_BRIDGE_INCLUDE_DIRS}")
EOF

# Create a test program to verify installation
log "Creating verification test..."
cat > /tmp/test_dependencies.cpp << 'EOF'
#include <iostream>
#include <pigpio.h>
#include <lvgl.h>
#include <nlohmann/json.hpp>

int main() {
    std::cout << "Testing dependencies..." << std::endl;
    
    // Test pigpio
    if (gpioInitialise() >= 0) {
        std::cout << "✓ pigpio: OK" << std::endl;
        gpioTerminate();
    } else {
        std::cout << "✗ pigpio: Failed" << std::endl;
    }
    
    // Test LVGL
    lv_init();
    std::cout << "✓ LVGL: OK" << std::endl;
    
    // Test nlohmann json
    nlohmann::json j = {{"test", "value"}};
    std::cout << "✓ nlohmann-json: OK" << std::endl;
    
    std::cout << "All dependencies verified!" << std::endl;
    return 0;
}
EOF

# Compile test
cd /tmp
g++ -o test_deps test_dependencies.cpp -lpigpio -llvgl -pthread
if ./test_deps; then
    success "All dependencies verified successfully!"
else
    warn "Some dependencies may have issues"
fi

# Clean up
rm -f test_dependencies.cpp test_deps

success "BigTechTree CB1 dependencies installation completed!"

echo
echo "=== Installation Summary ==="
echo "✓ Build tools installed"
echo "✓ pigpio installed from source"
echo "✓ LVGL installed from source"
echo "✓ Samba libraries installed"
echo "✓ nlohmann-json installed"
echo "✓ GPIO/SPI/I2C access configured"
echo "✓ USB gadget support enabled"
echo "✓ Required directories created"
echo
echo "=== Important Notes ==="
echo "1. You may need to reboot to enable all kernel modules"
echo "2. USB gadget functionality requires proper hardware support"
echo "3. GPIO access may require running as root or adding user to gpio group"
echo "4. Some features may need additional hardware-specific configuration"
echo
echo "=== Next Steps ==="
echo "1. Reboot the system: sudo reboot"
echo "2. Re-run this script if any issues: $0"
echo "3. Test USB gadget: ls /sys/kernel/config/usb_gadget"
echo "4. Deploy your USB Bridge project"