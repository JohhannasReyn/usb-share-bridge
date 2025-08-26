#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include <fstream>

const std::string ConfigManager::SYSTEM_CONFIG_PATH = "/etc/usb-bridge/system.json";
const std::string ConfigManager::NETWORK_CONFIG_PATH = "/etc/usb-bridge/network.json";
const std::string ConfigManager::UI_CONFIG_PATH = "/etc/usb-bridge/ui.json";

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig() {
    LOG_INFO("Loading configuration files", "CONFIG");
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    ensureConfigDirectories();
    
    bool success = true;
    
    // Load network configuration
    if (FileUtils::fileExists(NETWORK_CONFIG_PATH)) {
        try {
            std::string content = FileUtils::readTextFile(NETWORK_CONFIG_PATH);
            m_networkConfig = nlohmann::json::parse(content);
            LOG_INFO("Network configuration loaded", "CONFIG");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse network config: " + std::string(e.what()), "CONFIG");
            success = false;
        }
    } else {
        LOG_WARNING("Network config file not found, using defaults", "CONFIG");
        m_networkConfig = getDefaultNetworkConfig();
    }
    
    // Load UI configuration
    if (FileUtils::fileExists(UI_CONFIG_PATH)) {
        try {
            std::string content = FileUtils::readTextFile(UI_CONFIG_PATH);
            m_uiConfig = nlohmann::json::parse(content);
            LOG_INFO("UI configuration loaded", "CONFIG");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UI config: " + std::string(e.what()), "CONFIG");
            success = false;
        }
    } else {
        LOG_WARNING("UI config file not found, using defaults", "CONFIG");
        m_uiConfig = getDefaultUIConfig();
    }
    
    return success;
}

bool ConfigManager::saveConfig() {
    LOG_INFO("Saving configuration files", "CONFIG");
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    ensureConfigDirectories();
    
    bool success = true;
    
    // Save system configuration
    try {
        std::string content = m_systemConfig.dump(4);
        if (!FileUtils::writeTextFile(SYSTEM_CONFIG_PATH, content)) {
            LOG_ERROR("Failed to write system config file", "CONFIG");
            success = false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize system config: " + std::string(e.what()), "CONFIG");
        success = false;
    }
    
    // Save network configuration
    try {
        std::string content = m_networkConfig.dump(4);
        if (!FileUtils::writeTextFile(NETWORK_CONFIG_PATH, content)) {
            LOG_ERROR("Failed to write network config file", "CONFIG");
            success = false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize network config: " + std::string(e.what()), "CONFIG");
        success = false;
    }
    
    // Save UI configuration
    try {
        std::string content = m_uiConfig.dump(4);
        if (!FileUtils::writeTextFile(UI_CONFIG_PATH, content)) {
            LOG_ERROR("Failed to write UI config file", "CONFIG");
            success = false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize UI config: " + std::string(e.what()), "CONFIG");
        success = false;
    }
    
    if (success) {
        LOG_INFO("Configuration saved successfully", "CONFIG");
    }
    
    return success;
}

template<typename T>
T ConfigManager::getValue(const std::string& key, const T& defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Parse key path (e.g., "system.device_name" or "network.wifi.enabled")
    std::vector<std::string> keyParts;
    std::istringstream iss(key);
    std::string part;
    while (std::getline(iss, part, '.')) {
        keyParts.push_back(part);
    }
    
    if (keyParts.empty()) {
        return defaultValue;
    }
    
    // Select the appropriate config object
    nlohmann::json* config = nullptr;
    if (keyParts[0] == "system") {
        config = &m_systemConfig;
    } else if (keyParts[0] == "network") {
        config = &m_networkConfig;
    } else if (keyParts[0] == "ui") {
        config = &m_uiConfig;
    } else {
        return defaultValue;
    }
    
    // Navigate through the JSON structure
    nlohmann::json* current = config;
    for (size_t i = 1; i < keyParts.size(); i++) {
        if (current->contains(keyParts[i])) {
            current = &(*current)[keyParts[i]];
        } else {
            return defaultValue;
        }
    }
    
    try {
        return current->get<T>();
    } catch (const std::exception&) {
        return defaultValue;
    }
}

template<typename T>
void ConfigManager::setValue(const std::string& key, const T& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Parse key path
    std::vector<std::string> keyParts;
    std::istringstream iss(key);
    std::string part;
    while (std::getline(iss, part, '.')) {
        keyParts.push_back(part);
    }
    
    if (keyParts.empty()) {
        return;
    }
    
    // Select the appropriate config object
    nlohmann::json* config = nullptr;
    if (keyParts[0] == "system") {
        config = &m_systemConfig;
    } else if (keyParts[0] == "network") {
        config = &m_networkConfig;
    } else if (keyParts[0] == "ui") {
        config = &m_uiConfig;
    } else {
        return;
    }
    
    // Navigate and create path if necessary
    nlohmann::json* current = config;
    for (size_t i = 1; i < keyParts.size() - 1; i++) {
        if (!current->contains(keyParts[i])) {
            (*current)[keyParts[i]] = nlohmann::json::object();
        }
        current = &(*current)[keyParts[i]];
    }
    
    // Set the value
    (*current)[keyParts.back()] = value;
}

std::string ConfigManager::getStringValue(const std::string& key, const std::string& defaultValue) {
    return getValue<std::string>(key, defaultValue);
}

int ConfigManager::getIntValue(const std::string& key, int defaultValue) {
    return getValue<int>(key, defaultValue);
}

bool ConfigManager::getBoolValue(const std::string& key, bool defaultValue) {
    return getValue<bool>(key, defaultValue);
}

nlohmann::json ConfigManager::getSection(const std::string& section) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (section == "system") {
        return m_systemConfig;
    } else if (section == "network") {
        return m_networkConfig;
    } else if (section == "ui") {
        return m_uiConfig;
    }
    
    return nlohmann::json::object();
}

void ConfigManager::setSection(const std::string& section, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (section == "system") {
        m_systemConfig = data;
    } else if (section == "network") {
        m_networkConfig = data;
    } else if (section == "ui") {
        m_uiConfig = data;
    }
}

void ConfigManager::ensureConfigDirectories() {
    std::string configDir = "/etc/usb-bridge";
    if (!FileUtils::directoryExists(configDir)) {
        FileUtils::createDirectory(configDir);
    }
}

nlohmann::json ConfigManager::getDefaultSystemConfig() {
    return nlohmann::json{
        {"system", {
            {"device_name", "USB Bridge Device"},
            {"version", "1.0.0"},
            {"auto_start", true},
            {"log_level", "INFO"}
        }},
        {"usb", {
            {"max_hosts", 2},
            {"host1", {
                {"enabled", true},
                {"device_path", "/dev/usb1"},
                {"mount_point", "/mnt/usb1"}
            }},
            {"host2", {
                {"enabled", true},
                {"device_path", "/dev/usb2"},
                {"mount_point", "/mnt/usb2"}
            }},
            {"auto_mount", true},
            {"file_system_types", {"ntfs", "fat32", "exfat", "ext4"}}
        }},
        {"storage", {
            {"mount_point", "/mnt/usb_bridge"},
            {"monitor_interval", 5},
            {"cache_thumbnails", true},
            {"max_cache_size", 104857600}
        }},
        {"display", {
            {"width", 480},
            {"height", 320},
            {"brightness", 80},
            {"timeout", 300},
            {"orientation", "landscape"}
        }},
        {"logging", {
            {"max_file_size", 10485760},
            {"max_files", 5},
            {"log_rotation", true},
            {"console_output", true}
        }}
    };
}

nlohmann::json ConfigManager::getDefaultNetworkConfig() {
    return nlohmann::json{
        {"network", {
            {"enabled", false},
            {"auto_start", false},
            {"interface_priority", {"wlan0", "eth0"}}
        }},
        {"wifi", {
            {"enabled", true},
            {"auto_connect", true},
            {"scan_interval", 30},
            {"connection_timeout", 30},
            {"saved_networks", nlohmann::json::array()}
        }},
        {"ethernet", {
            {"enabled", true},
            {"dhcp", true},
            {"static_ip", ""},
            {"subnet_mask", ""},
            {"gateway", ""},
            {"dns_servers", {"8.8.8.8", "8.8.4.4"}}
        }},
        {"services", {
            {"smb", {
                {"enabled", true},
                {"port", 445},
                {"workgroup", "WORKGROUP"},
                {"server_name", "USB-BRIDGE"},
                {"share_name", "USB_SHARE"},
                {"read_only", false},
                {"guest_access", true},
                {"users", nlohmann::json::array()}
            }},
            {"http", {
                {"enabled", true},
                {"port", 8080},
                {"document_root", "/web"},
                {"directory_listing", true},
                {"file_download", true},
                {"upload_enabled", false}
            }},
            {"ssh", {
                {"enabled", false},
                {"port", 22},
                {"password_auth", false},
                {"key_auth", true}
            }}
        }},
        {"security", {
            {"firewall_enabled", true},
            {"allowed_ports", {22, 80, 445, 8080}},
            {"block_unknown", false}
        }}
    };
}

nlohmann::json ConfigManager::getDefaultUIConfig() {
    return nlohmann::json{
        {"ui", {
            {"theme", "default"},
            {"color_scheme", "blue"},
            {"font_size", "medium"},
            {"animation_speed", "normal"},
            {"touch_sensitivity", 5}
        }},
        {"screens", {
            {"home", {
                {"show_status", true},
                {"show_quick_actions", true},
                {"auto_refresh", true}
            }},
            {"file_explorer", {
                {"view_mode", "list"},
                {"show_hidden", false},
                {"sort_by", "name"},
                {"sort_order", "ascending"},
                {"thumbnail_size", "medium"}
            }},
            {"logs", {
                {"max_entries", 100},
                {"auto_refresh", true},
                {"refresh_interval", 5},
                {"default_filter", "all"}
            }},
            {"settings", {
                {"confirm_changes", true},
                {"auto_save", false}
            }},
            {"network", {
                {"show_passwords", false},
                {"auto_scan", true},
                {"scan_interval", 30}
            }}
        }},
        {"notifications", {
            {"enabled", true},
            {"duration", 3},
            {"position", "top_right"},
            {"sound", false}
        }}
    };
}

// Explicit template instantiations for common types
template std::string ConfigManager::getValue<std::string>(const std::string&, const std::string&);
template int ConfigManager::getValue<int>(const std::string&, const int&);
template bool ConfigManager::getValue<bool>(const std::string&, const bool&);
template void ConfigManager::setValue<std::string>(const std::string&, const std::string&);
template void ConfigManager::setValue<int>(const std::string&, const int&);
template void ConfigManager::setValue<bool>(const std::string&, const bool&);
