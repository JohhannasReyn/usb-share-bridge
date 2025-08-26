//===== src/core/HostController.cpp (Complete Implementation) =====
#include "core/HostController.hpp"
#include "utils/Logger.hpp"
#include <sys/mount.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>

HostController::HostController(int hostId)
    : m_hostId(hostId)
    , m_status(ConnectionStatus::DISCONNECTED)
    , m_accessEnabled(true)
    , m_shouldRun(false)
{
}

HostController::~HostController() {
    disconnect();
}

bool HostController::connect() {
    if (m_status == ConnectionStatus::CONNECTED || m_status == ConnectionStatus::CONNECTING) {
        return true;
    }
    
    LOG_INFO("Connecting USB host " + std::to_string(m_hostId), "HOST");
    
    m_status = ConnectionStatus::CONNECTING;
    notifyStatusChange();
    
    m_shouldRun = true;
    m_connectionThread = std::thread(&HostController::connectionLoop, this);
    
    return true;
}

bool HostController::disconnect() {
    if (m_status == ConnectionStatus::DISCONNECTED) {
        return true;
    }
    
    LOG_INFO("Disconnecting USB host " + std::to_string(m_hostId), "HOST");
    
    m_shouldRun = false;
    
    if (m_connectionThread.joinable()) {
        m_connectionThread.join();
    }
    
    // Cleanup USB gadget configuration
    cleanupUsbGadget();
    
    m_status = ConnectionStatus::DISCONNECTED;
    notifyStatusChange();
    
    return true;
}

void HostController::setStatusCallback(std::function<void(int, ConnectionStatus)> callback) {
    m_statusCallback = callback;
}

void HostController::enableAccess() {
    m_accessEnabled = true;
    LOG_INFO("Access enabled for USB host " + std::to_string(m_hostId), "HOST");
}

void HostController::disableAccess() {
    m_accessEnabled = false;
    LOG_INFO("Access disabled for USB host " + std::to_string(m_hostId), "HOST");
}

void HostController::connectionLoop() {
    while (m_shouldRun) {
        try {
            // Check if USB gadget module is available
            std::string gadgetPath = "/sys/kernel/config/usb_gadget/usb" + std::to_string(m_hostId);
            
            if (std::filesystem::exists("/sys/kernel/config/usb_gadget")) {
                if (m_status != ConnectionStatus::CONNECTED) {
                    // Configure USB gadget for mass storage
                    if (configureUsbGadget()) {
                        m_status = ConnectionStatus::CONNECTED;
                        LOG_INFO("USB host " + std::to_string(m_hostId) + " connected", "HOST");
                        notifyStatusChange();
                    } else {
                        m_status = ConnectionStatus::ERROR;
                        LOG_ERROR("Failed to configure USB gadget for host " + std::to_string(m_hostId), "HOST");
                        notifyStatusChange();
                    }
                }
                
                // Monitor connection health
                if (m_status == ConnectionStatus::CONNECTED) {
                    if (!isGadgetActive()) {
                        LOG_WARNING("USB gadget became inactive for host " + std::to_string(m_hostId), "HOST");
                        m_status = ConnectionStatus::DISCONNECTED;
                        notifyStatusChange();
                    }
                }
                
            } else {
                if (m_status == ConnectionStatus::CONNECTED) {
                    m_status = ConnectionStatus::DISCONNECTED;
                    LOG_INFO("USB gadget subsystem unavailable for host " + std::to_string(m_hostId), "HOST");
                    notifyStatusChange();
                }
            }
            
            // Check connection status periodically
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in host controller loop: " + std::string(e.what()), "HOST");
            m_status = ConnectionStatus::ERROR;
            notifyStatusChange();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

bool HostController::configureUsbGadget() {
    std::string gadgetName = "usb" + std::to_string(m_hostId);
    std::string gadgetPath = "/sys/kernel/config/usb_gadget/" + gadgetName;
    
    try {
        // Cleanup any existing configuration first
        cleanupUsbGadget();
        
        // Create gadget directory
        if (!std::filesystem::create_directories(gadgetPath)) {
            LOG_ERROR("Failed to create gadget directory: " + gadgetPath, "HOST");
            return false;
        }
        
        // Configure USB device descriptor
        writeGadgetFile(gadgetPath + "/idVendor", "0x1d6b");   // Linux Foundation
        writeGadgetFile(gadgetPath + "/idProduct", "0x0104");  // Multifunction Composite Gadget
        writeGadgetFile(gadgetPath + "/bcdDevice", "0x0100");  // Version 1.0.0
        writeGadgetFile(gadgetPath + "/bcdUSB", "0x0200");     // USB 2.0
        writeGadgetFile(gadgetPath + "/bDeviceClass", "0x00");
        writeGadgetFile(gadgetPath + "/bDeviceSubClass", "0x00");
        writeGadgetFile(gadgetPath + "/bDeviceProtocol", "0x00");
        writeGadgetFile(gadgetPath + "/bMaxPacketSize0", "0x40");
        
        // Create strings directory and configure device strings
        std::string stringsPath = gadgetPath + "/strings/0x409";
        if (!std::filesystem::create_directories(stringsPath)) {
            LOG_ERROR("Failed to create strings directory", "HOST");
            return false;
        }
        
        writeGadgetFile(stringsPath + "/serialnumber", "USBBRIDGE" + std::to_string(m_hostId));
        writeGadgetFile(stringsPath + "/manufacturer", "USB Bridge Device");
        writeGadgetFile(stringsPath + "/product", "Mass Storage Gadget " + std::to_string(m_hostId));
        
        // Create mass storage function
        std::string functionPath = gadgetPath + "/functions/mass_storage.usb" + std::to_string(m_hostId);
        if (!std::filesystem::create_directories(functionPath)) {
            LOG_ERROR("Failed to create mass storage function", "HOST");
            return false;
        }
        
        // Configure mass storage backing file
        if (!configureMassStorageBacking(functionPath)) {
            LOG_ERROR("Failed to configure mass storage backing", "HOST");
            return false;
        }
        
        // Create configuration
        std::string configPath = gadgetPath + "/configs/c.1";
        if (!std::filesystem::create_directories(configPath)) {
            LOG_ERROR("Failed to create configuration directory", "HOST");
            return false;
        }
        
        writeGadgetFile(configPath + "/MaxPower", "250");
        writeGadgetFile(configPath + "/bmAttributes", "0x80"); // Bus-powered
        
        // Create configuration strings
        std::string configStringsPath = configPath + "/strings/0x409";
        if (!std::filesystem::create_directories(configStringsPath)) {
            LOG_ERROR("Failed to create config strings directory", "HOST");
            return false;
        }
        
        writeGadgetFile(configStringsPath + "/configuration", "Mass Storage Configuration");
        
        // Link function to configuration
        std::string linkPath = configPath + "/mass_storage.usb" + std::to_string(m_hostId);
        if (std::filesystem::exists(linkPath)) {
            std::filesystem::remove(linkPath);
        }
        
        if (symlink(functionPath.c_str(), linkPath.c_str()) != 0) {
            LOG_ERROR("Failed to create symlink from function to config", "HOST");
            return false;
        }
        
        // Enable the gadget by writing to UDC
        std::string udcName = findAvailableUDC();
        if (udcName.empty()) {
            LOG_ERROR("No available UDC found for host " + std::to_string(m_hostId), "HOST");
            return false;
        }
        
        writeGadgetFile(gadgetPath + "/UDC", udcName);
        
        // Wait a moment for the gadget to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        LOG_INFO("USB gadget configured successfully for host " + std::to_string(m_hostId) + " on UDC " + udcName, "HOST");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception configuring USB gadget: " + std::string(e.what()), "HOST");
        cleanupUsbGadget(); // Cleanup on error
        return false;
    }
}

bool HostController::configureMassStorageBacking(const std::string& functionPath) {
    try {
        // Check if we have a mounted USB drive to share
        std::string usbMountPoint = "/mnt/usb_bridge";
        std::string backingFile = usbMountPoint + "/bridge_storage_" + std::to_string(m_hostId) + ".img";
        
        // If no USB drive is mounted, create a temporary backing file
        if (!std::filesystem::exists(usbMountPoint) || !std::filesystem::is_directory(usbMountPoint)) {
            backingFile = "/tmp/usb_bridge_" + std::to_string(m_hostId) + ".img";
            LOG_WARNING("No USB storage mounted, using temporary backing file: " + backingFile, "HOST");
        }
        
        // Create backing file if it doesn't exist
        if (!std::filesystem::exists(backingFile)) {
            createBackingFile(backingFile);
        }
        
        // Configure the LUN (Logical Unit Number)
        std::string lunPath = functionPath + "/lun.0";
        
        writeGadgetFile(lunPath + "/file", backingFile);
        writeGadgetFile(lunPath + "/removable", "1");
        writeGadgetFile(lunPath + "/cdrom", "0");
        writeGadgetFile(lunPath + "/ro", m_accessEnabled ? "0" : "1"); // Read-only if access disabled
        writeGadgetFile(lunPath + "/nofua", "1"); // Disable FUA for better performance
        
        LOG_INFO("Mass storage backing configured: " + backingFile, "HOST");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to configure mass storage backing: " + std::string(e.what()), "HOST");
        return false;
    }
}

void HostController::createBackingFile(const std::string& filePath) {
    LOG_INFO("Creating backing file: " + filePath, "HOST");
    
    // Create a 1GB backing file
    const size_t fileSize = 1024 * 1024 * 1024; // 1GB
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create backing file: " + filePath);
    }
    
    // Write sparse file (seek to end and write one byte)
    file.seekp(fileSize - 1);
    file.write("\0", 1);
    file.close();
    
    // Format the file as FAT32
    std::string formatCmd = "mkfs.vfat -F 32 -n \"USBBRIDGE\" \"" + filePath + "\" >/dev/null 2>&1";
    int result = system(formatCmd.c_str());
    
    if (result != 0) {
        LOG_WARNING("Failed to format backing file as FAT32", "HOST");
    } else {
        LOG_INFO("Backing file formatted as FAT32", "HOST");
    }
}

std::string HostController::findAvailableUDC() {
    std::string udcDir = "/sys/class/udc";
    
    try {
        if (!std::filesystem::exists(udcDir)) {
            LOG_ERROR("UDC directory not found: " + udcDir, "HOST");
            return "";
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(udcDir)) {
            std::string udcName = entry.path().filename().string();
            
            // Check if this UDC is already in use
            std::string statePath = entry.path().string() + "/state";
            std::ifstream stateFile(statePath);
            std::string state;
            
            if (stateFile.is_open() && std::getline(stateFile, state)) {
                // UDC is available if it's not attached or if it's in a configurable state
                if (state == "not attached" || state == "default" || state.empty()) {
                    LOG_INFO("Found available UDC: " + udcName + " (state: " + state + ")", "HOST");
                    return udcName;
                }
            }
        }
        
        LOG_WARNING("No available UDC found", "HOST");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error finding available UDC: " + std::string(e.what()), "HOST");
    }
    
    return "";
}

bool HostController::isGadgetActive() {
    std::string gadgetName = "usb" + std::to_string(m_hostId);
    std::string gadgetPath = "/sys/kernel/config/usb_gadget/" + gadgetName;
    std::string udcPath = gadgetPath + "/UDC";
    
    try {
        if (!std::filesystem::exists(udcPath)) {
            return false;
        }
        
        std::ifstream udcFile(udcPath);
        std::string udcName;
        
        if (!udcFile.is_open() || !std::getline(udcFile, udcName)) {
            return false;
        }
        
        // If UDC is empty, gadget is not active
        if (udcName.empty()) {
            return false;
        }
        
        // Check UDC state
        std::string statePath = "/sys/class/udc/" + udcName + "/state";
        std::ifstream stateFile(statePath);
        std::string state;
        
        if (stateFile.is_open() && std::getline(stateFile, state)) {
            return state == "configured" || state == "suspended";
        }
        
        return true; // Assume active if we can't read state
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking gadget status: " + std::string(e.what()), "HOST");
        return false;
    }
}

void HostController::cleanupUsbGadget() {
    std::string gadgetName = "usb" + std::to_string(m_hostId);
    std::string gadgetPath = "/sys/kernel/config/usb_gadget/" + gadgetName;
    
    try {
        if (!std::filesystem::exists(gadgetPath)) {
            return; // Nothing to cleanup
        }
        
        LOG_INFO("Cleaning up USB gadget configuration for host " + std::to_string(m_hostId), "HOST");
        
        // Disable gadget first
        std::string udcPath = gadgetPath + "/UDC";
        if (std::filesystem::exists(udcPath)) {
            writeGadgetFile(udcPath, "");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Remove configuration links
        std::string configPath = gadgetPath + "/configs/c.1";
        if (std::filesystem::exists(configPath)) {
            // Remove function links
            for (const auto& entry : std::filesystem::directory_iterator(configPath)) {
                if (std::filesystem::is_symlink(entry)) {
                    std::filesystem::remove(entry);
                }
            }
            
            // Remove config strings
            std::string configStringsPath = configPath + "/strings/0x409";
            if (std::filesystem::exists(configStringsPath)) {
                std::filesystem::remove_all(configStringsPath);
            }
            
            // Remove configuration directory
            std::filesystem::remove_all(configPath);
        }
        
        // Remove functions
        std::string functionsPath = gadgetPath + "/functions";
        if (std::filesystem::exists(functionsPath)) {
            std::filesystem::remove_all(functionsPath);
        }
        
        // Remove strings
        std::string stringsPath = gadgetPath + "/strings";
        if (std::filesystem::exists(stringsPath)) {
            std::filesystem::remove_all(stringsPath);
        }
        
        // Remove gadget directory
        std::filesystem::remove_all(gadgetPath);
        
        LOG_INFO("USB gadget cleanup completed for host " + std::to_string(m_hostId), "HOST");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error during USB gadget cleanup: " + std::string(e.what()), "HOST");
    }
}

bool HostController::writeGadgetFile(const std::string& filePath, const std::string& content) {
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open gadget file for writing: " + filePath, "HOST");
            return false;
        }
        
        file << content;
        if (!file.good()) {
            LOG_ERROR("Failed to write to gadget file: " + filePath, "HOST");
            return false;
        }
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception writing to gadget file " + filePath + ": " + std::string(e.what()), "HOST");
        return false;
    }
}

void HostController::notifyStatusChange() {
    if (m_statusCallback) {
        m_statusCallback(m_hostId, m_status);
    }
}

// Additional utility methods for advanced functionality

bool HostController::updateAccessMode(bool readOnly) {
    if (m_status != ConnectionStatus::CONNECTED) {
        return false;
    }
    
    try {
        std::string gadgetName = "usb" + std::to_string(m_hostId);
        std::string gadgetPath = "/sys/kernel/config/usb_gadget/" + gadgetName;
        std::string lunPath = gadgetPath + "/functions/mass_storage.usb" + std::to_string(m_hostId) + "/lun.0/ro";
        
        return writeGadgetFile(lunPath, readOnly ? "1" : "0");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to update access mode: " + std::string(e.what()), "HOST");
        return false;
    }
}

bool HostController::changeBackingFile(const std::string& newBackingFile) {
    if (m_status != ConnectionStatus::CONNECTED) {
        return false;
    }
    
    try {
        std::string gadgetName = "usb" + std::to_string(m_hostId);
        std::string gadgetPath = "/sys/kernel/config/usb_gadget/" + gadgetName;
        std::string filePath = gadgetPath + "/functions/mass_storage.usb" + std::to_string(m_hostId) + "/lun.0/file";
        
        // Disconnect first
        writeGadgetFile(gadgetPath + "/UDC", "");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Change backing file
        bool success = writeGadgetFile(filePath, newBackingFile);
        
        // Reconnect
        std::string udcName = findAvailableUDC();
        if (!udcName.empty()) {
            writeGadgetFile(gadgetPath + "/UDC", udcName);
        }
        
        if (success) {
            LOG_INFO("Changed backing file to: " + newBackingFile, "HOST");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to change backing file: " + std::string(e.what()), "HOST");
        return false;
    }
}

std::string HostController::getConnectionInfo() const {
    std::string info = "Host " + std::to_string(m_hostId) + ": ";
    
    switch (m_status) {
        case ConnectionStatus::DISCONNECTED:
            info += "Disconnected";
            break;
        case ConnectionStatus::CONNECTING:
            info += "Connecting...";
            break;
        case ConnectionStatus::CONNECTED:
            info += "Connected";
            if (!m_accessEnabled) {
                info += " (Read-Only)";
            }
            break;
        case ConnectionStatus::ERROR:
            info += "Error";
            break;
    }
    
    return info;
}