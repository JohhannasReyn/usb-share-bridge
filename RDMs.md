___Recommended deployment methods___

## **Method 1: SSH Development Workflow (Recommended)**

This is the most efficient for development and testing:

### **1. Setup SSH Connection**
```bash
# From WSL/VS Code terminal
ssh pi@<your-board-ip>

# Or if you haven't set it up yet:
# Find the board's IP (if connected via Ethernet/WiFi)
nmap -sn 192.168.1.0/24  # Adjust subnet as needed
```

### **2. Install Development Tools on the Board**
```bash
# On the BigTechTree Pi
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
  libsamba-dev liblvgl-dev libpigpio-dev libwiringpi-dev \
  nlohmann-json3-dev samba samba-common-bin python3-dev \
  libssl-dev libi2c-dev

# Install LVGL if not available via package manager
cd /tmp
git clone https://github.com/lvgl/lvgl.git
cd lvgl
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### **3. Transfer and Build Project**
```bash
# From your WSL/VS Code terminal
# Option A: Use rsync (recommended)
rsync -av --exclude='.git' --exclude='build' \
  /path/to/usb-share-bridge/ pi@<board-ip>:~/usb-share-bridge/

# Option B: Use git
# On the board:
git clone https://github.com/JohhannasReyn/usb-share-bridge.git ~/usb-share-bridge
cd ~/usb-share-bridge
```

### **4. Build on the Board**
```bash
# SSH into the board
ssh pi@<board-ip>
cd ~/usb-share-bridge

# Create build directory
mkdir build && cd build

# Configure and build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Install
sudo make install
```

## **Method 2: Cross-Compilation (Advanced)**

For faster builds, compile on your development machine:

### **1. Setup Cross-Compiler**
```bash
# In WSL
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Download board's sysroot (libraries)
mkdir ~/pi-sysroot
rsync -av pi@<board-ip>:/lib ~/pi-sysroot/
rsync -av pi@<board-ip>:/usr ~/pi-sysroot/
```

### **2. Cross-Compile Build**
```bash
# Create toolchain file
cat > arm64-toolchain.cmake << EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT ~/pi-sysroot)
EOF

# Build
mkdir build-arm64 && cd build-arm64
cmake -DCMAKE_TOOLCHAIN_FILE=../arm64-toolchain.cmake ..
make -j$(nproc)

# Deploy
scp usb-share-bridge pi@<board-ip>:~/
```

## **Method 3: Direct SD Card Method**

If you have physical access and want to prepare everything offline:

### **1. Mount SD Card**
```bash
# In WSL (assuming SD card is mounted)
sudo mkdir /mnt/pi-root
sudo mount /dev/sdX2 /mnt/pi-root  # Adjust device as needed
```

### **2. Copy Project and Cross-Compile**
```bash
# Copy source
sudo cp -r usb-share-bridge /mnt/pi-root/home/pi/

# Or copy pre-compiled binary
sudo cp build-arm64/usb-share-bridge /mnt/pi-root/usr/local/bin/
sudo cp -r config /mnt/pi-root/etc/usb-bridge/
sudo cp scripts/* /mnt/pi-root/usr/local/bin/
```

## **Quick Start Instructions:**

### **1. Prepare the Script**
```bash
# In your VS Code/WSL terminal
chmod +x deploy.sh
```

### **2. Find Your Board's IP**
```bash
# If connected via Ethernet/WiFi
nmap -sn 192.168.1.0/24  # Adjust your subnet

# Or check your router's admin interface
# Or connect via serial console initially
```

### **3. Setup SSH (First Time Only)**
```bash
# Generate SSH key if you don't have one
ssh-keygen -t rsa -b 4096

# Copy to board (you'll need password first time)
ssh-copy-id pi@<board-ip>
```

### **4. Deploy Everything**
```bash
# Full deployment (recommended first time)
./deploy.sh 192.168.1.100

# Or step by step:
./deploy.sh 192.168.1.100 deps     # Install dependencies
./deploy.sh 192.168.1.100 deploy   # Copy source
./deploy.sh 192.168.1.100 build    # Compile
./deploy.sh 192.168.1.100 install  # Install
./deploy.sh 192.168.1.100 start    # Start service
```

### **5. Development Workflow**
```bash
# After making changes:
./deploy.sh 192.168.1.100 deploy   # Copy new source
./deploy.sh 192.168.1.100 build    # Rebuild
./deploy.sh 192.168.1.100 install  # Reinstall

# Or use VS Code remote development:
code --remote ssh-remote+pi@192.168.1.100 /home/pi/usb-share-bridge
```

### **6. Monitor and Debug**
```bash
# Check status
./deploy.sh 192.168.1.100 status

# SSH aliases (after deployment)
ssh pi@192.168.1.100
bridge-status    # Check service
bridge-logs      # View logs
bridge-restart   # Restart service
```

## **Recommended Approach:**

1. **Start with SSH Method** - Most flexible for development
2. **Use the deployment script** - Automates everything
3. **Setup VS Code Remote** - Best development experience
4. **Use cross-compilation later** - When you need faster builds


The SSH method with the deployment script is your best bet for getting started quickly and maintaining a good development workflow!
