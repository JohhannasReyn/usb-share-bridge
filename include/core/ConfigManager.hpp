#pragma once

#include <string>
#include <map>
#include <any>
#include <mutex>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    static ConfigManager& instance();
    
    bool loadConfig();
    bool saveConfig();
    
    // Generic configuration access
    template<typename T>
    T getValue(const std::string& key, const T& defaultValue = T{});
    
    template<typename T>
    void setValue(const std::string& key, const T& value);
    
    // Specialized getters
    std::string getStringValue(const std::string& key, const std::string& defaultValue = "");
    int getIntValue(const std::string& key, int defaultValue = 0);
    bool getBoolValue(const std::string& key, bool defaultValue = false);
    
    // Configuration sections
    nlohmann::json getSection(const std::string& section);
    void setSection(const std::string& section, const nlohmann::json& data);

private:
    ConfigManager() = default;
    void ensureConfigDirectories();
    
    std::mutex m_mutex;
    nlohmann::json m_systemConfig;
    nlohmann::json m_networkConfig;
    nlohmann::json m_uiConfig;
    
    static const std::string SYSTEM_CONFIG_PATH;
    static const std::string NETWORK_CONFIG_PATH;
    static const std::string UI_CONFIG_PATH;
};