#include "gui/screens/ScreenNetwork.hpp"
#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"

ScreenNetwork::ScreenNetwork(const std::string& name, UsbBridge* bridge)
    : Screen(name, bridge)
    , m_passwordDialog(nullptr)
{
}

bool ScreenNetwork::create() {
    m_container = lv_obj_create(nullptr);
    lv_obj_set_size(m_container, 480, 290);
    lv_obj_set_pos(m_container, 0, 30);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xFFFFFF), 0);
    
    // Title and home button
    lv_obj_t* title = lv_label_create(m_container);
    lv_label_set_text(title, "Network Configuration");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title, 10, 10);
    
    lv_obj_t* homeButton = lv_btn_create(m_container);
    lv_obj_set_size(homeButton, 50, 30);
    lv_obj_set_pos(homeButton, 420, 10);
    lv_obj_add_event_cb(homeButton, [](lv_event_t* e) {
        ScreenNetwork* screen = static_cast<ScreenNetwork*>(lv_event_get_user_data(e));
        screen->navigateToScreen("home");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* homeLabel = lv_label_create(homeButton);
    lv_label_set_text(homeLabel, LV_SYMBOL_HOME);
    lv_obj_center(homeLabel);
    
    createWifiSection();
    createEthernetSection();
    createServiceStatus();
    
    return true;
}

void ScreenNetwork::createWifiSection() {
    // WiFi section header
    lv_obj_t* wifiHeader = lv_label_create(m_container);
    lv_label_set_text(wifiHeader, "WiFi Networks");
    lv_obj_set_style_text_font(wifiHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(wifiHeader, 10, 50);
    
    // Scan button
    m_scanButton = lv_btn_create(m_container);
    lv_obj_set_size(m_scanButton, 80, 30);
    lv_obj_set_pos(m_scanButton, 390, 45);
    lv_obj_add_event_cb(m_scanButton, [](lv_event_t* e) {
        ScreenNetwork* screen = static_cast<ScreenNetwork*>(lv_event_get_user_data(e));
        screen->scanWifiNetworks();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* scanLabel = lv_label_create(m_scanButton);
    lv_label_set_text(scanLabel, "Scan");
    lv_obj_center(scanLabel);
    
    // WiFi list
    m_wifiList = lv_list_create(m_container);
    lv_obj_set_size(m_wifiList, 220, 120);
    lv_obj_set_pos(m_wifiList, 10, 80);
    lv_obj_set_style_bg_color(m_wifiList, lv_color_white(), 0);
    lv_obj_set_style_border_width(m_wifiList, 1, 0);
    lv_obj_set_style_border_color(m_wifiList, lv_color_hex(0xE0E0E0), 0);
    
    // Connection status
    m_connectionStatus = lv_label_create(m_container);
    lv_label_set_text(m_connectionStatus, "Status: Not connected");
    lv_obj_set_pos(m_connectionStatus, 10, 210);
    lv_obj_set_style_text_font(m_connectionStatus, &lv_font_montserrat_12, 0);
}

void ScreenNetwork::createEthernetSection() {
    // Ethernet section header
    lv_obj_t* ethHeader = lv_label_create(m_container);
    lv_label_set_text(ethHeader, "Ethernet");
    lv_obj_set_style_text_font(ethHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(ethHeader, 250, 50);
    
    // Ethernet status
    m_ethernetStatus = lv_label_create(m_container);
    lv_label_set_text(m_ethernetStatus, "Status: Disconnected");
    lv_obj_set_pos(m_ethernetStatus, 250, 80);
    lv_obj_set_style_text_font(m_ethernetStatus, &lv_font_montserrat_12, 0);
}

void ScreenNetwork::createServiceStatus() {
    // Services section
    lv_obj_t* servicesHeader = lv_label_create(m_container);
    lv_label_set_text(servicesHeader, "Network Services");
    lv_obj_set_style_text_font(servicesHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(servicesHeader, 250, 110);
    
    // SMB service toggle
    lv_obj_t* smbLabel = lv_label_create(m_container);
    lv_label_set_text(smbLabel, "SMB/CIFS:");
    lv_obj_set_pos(smbLabel, 250, 140);
    
    m_smbSwitch = lv_switch_create(m_container);
    lv_obj_set_pos(m_smbSwitch, 330, 140);
    lv_obj_add_event_cb(m_smbSwitch, [](lv_event_t* e) {
        ScreenNetwork* screen = static_cast<ScreenNetwork*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(screen->m_smbSwitch, LV_STATE_CHECKED);
        screen->onServiceToggle("smb", enabled);
    }, LV_EVENT_VALUE_CHANGED, this);
    
    // HTTP service toggle
    lv_obj_t* httpLabel = lv_label_create(m_container);
    lv_label_set_text(httpLabel, "HTTP Server:");
    lv_obj_set_pos(httpLabel, 250, 170);
    
    m_httpSwitch = lv_switch_create(m_container);
    lv_obj_set_pos(m_httpSwitch, 350, 170);
    lv_obj_add_event_cb(m_httpSwitch, [](lv_event_t* e) {
        ScreenNetwork* screen = static_cast<ScreenNetwork*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(screen->m_httpSwitch, LV_STATE_CHECKED);
        screen->onServiceToggle("http", enabled);
    }, LV_EVENT_VALUE_CHANGED, this);
}

void ScreenNetwork::show() {
    Screen::show();
    updateConnectionStatus();
    scanWifiNetworks();
}

void ScreenNetwork::update() {
    // Update connection status periodically
    static uint32_t lastUpdate = 0;
    uint32_t now = lv_tick_get();
    if (now - lastUpdate > 5000) { // Update every 5 seconds
        updateConnectionStatus();
        lastUpdate = now;
    }
}

void ScreenNetwork::scanWifiNetworks() {
    if (!m_bridge) {
        return;
    }
    
    LOG_INFO("Scanning for WiFi networks", "NETWORK_GUI");
    
    // Disable scan button temporarily
    lv_obj_add_state(m_scanButton, LV_STATE_DISABLED);
    
    // Clear existing list
    lv_obj_clean(m_wifiList);
    
    // Add scanning indicator
    lv_obj_t* scanningItem = lv_list_add_text(m_wifiList, "Scanning...");
    lv_obj_set_style_text_color(scanningItem, lv_color_hex(0x757575), 0);
    
    // In a real implementation, this would be done asynchronously
    // For now, simulate with a simple delay and populate with dummy data
    auto networks = m_bridge->getNetworkManager()->scanWifiNetworks();
    m_wifiNetworks = networks;
    
    updateWifiList();
    
    // Re-enable scan button
    lv_obj_clear_state(m_scanButton, LV_STATE_DISABLED);
}

void ScreenNetwork::updateWifiList() {
    if (!m_wifiList) {
        return;
    }
    
    // Clear existing items
    lv_obj_clean(m_wifiList);
    
    if (m_wifiNetworks.empty()) {
        lv_obj_t* noNetworksItem = lv_list_add_text(m_wifiList, "No networks found");
        lv_obj_set_style_text_color(noNetworksItem, lv_color_hex(0x757575), 0);
        return;
    }
    
    // Add network items
    for (size_t i = 0; i < m_wifiNetworks.size(); i++) {
        const WifiNetwork& network = m_wifiNetworks[i];
        
        // Create network item with signal strength indicator
        std::string itemText = network.ssid;
        if (network.signalStrength > 75) {
            itemText = LV_SYMBOL_WIFI " " + itemText;
        } else if (network.signalStrength > 50) {
            itemText = "📶 " + itemText;
        } else {
            itemText = "📱 " + itemText;
        }
        
        if (network.security != "OPEN") {
            itemText += " 🔒";
        }
        
        lv_obj_t* item = lv_list_add_btn(m_wifiList, nullptr, itemText.c_str());
        
        // Highlight connected network
        if (network.isConnected) {
            lv_obj_set_style_bg_color(item, lv_color_hex(0xE8F5E8), 0);
            lv_obj_set_style_text_color(item, lv_color_hex(0x4CAF50), 0);
        }
        
        // Store network index in user data
        lv_obj_set_user_data(item, (void*)i);
        
        // Add click event
        lv_obj_add_event_cb(item, [](lv_event_t* e) {
            lv_obj_t* item = lv_event_get_target(e);
            ScreenNetwork* screen = static_cast<ScreenNetwork*>(lv_event_get_user_data(e));
            size_t networkIndex = (size_t)lv_obj_get_user_data(item);
            
            if (networkIndex < screen->m_wifiNetworks.size()) {
                const WifiNetwork& network = screen->m_wifiNetworks[networkIndex];
                if (network.isConnected) {
                    screen->onWifiDisconnect();
                } else {
                    screen->onWifiConnect(network.ssid);
                }
            }
        }, LV_EVENT_CLICKED, this);
    }
}

void ScreenNetwork::updateConnectionStatus() {
    if (!m_bridge) {
        return;
    }
    
    auto networkManager = m_bridge->getNetworkManager();
    if (!networkManager) {
        return;
    }
    
    // Update WiFi status
    if (networkManager->getConnectionStatus() == NetworkManager::Status::CONNECTED) {
        auto activeInterface = networkManager->getActiveInterface();
        if (activeInterface.isWireless) {
            std::string statusText = "Status: Connected (" + activeInterface.ipAddress + ")";
            lv_label_set_text(m_connectionStatus, statusText.c_str());
            lv_obj_set_style_text_color(m_connectionStatus, lv_color_hex(0x4CAF50), 0);
        }
    } else {
        lv_label_set_text(m_connectionStatus, "Status: Not connected");
        lv_obj_set_style_text_color(m_connectionStatus, lv_color_hex(0x757575), 0);
    }
    
    // Update Ethernet status
    if (networkManager->isEthernetConnected()) {
        lv_label_set_text(m_ethernetStatus, "Status: Connected");
        lv_obj_set_style_text_color(m_ethernetStatus, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(m_ethernetStatus, "Status: Disconnected");
        lv_obj_set_style_text_color(m_ethernetStatus, lv_color_hex(0x757575), 0);
    }
    
    // Update service status
    bool servicesRunning = networkManager->areServicesRunning();
    if (servicesRunning) {
        lv_obj_add_state(m_smbSwitch, LV_STATE_CHECKED);
        lv_obj_add_state(m_httpSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(m_smbSwitch, LV_STATE_CHECKED);
        lv_obj_clear_state(m_httpSwitch, LV_STATE_CHECKED);
    }
}

void ScreenNetwork::onWifiConnect(const std::string& ssid) {
    m_selectedSsid = ssid;
    
    // Find the network to check if it needs a password
    auto it = std::find_if(m_wifiNetworks.begin(), m_wifiNetworks.end(),
        [&ssid](const WifiNetwork& network) { return network.ssid == ssid; });
    
    if (it != m_wifiNetworks.end() && it->security != "OPEN") {
        showPasswordDialog(ssid);
    } else {
        // Open network, connect directly
        if (m_bridge && m_bridge->getNetworkManager()) {
            bool success = m_bridge->getNetworkManager()->connectToWifi(ssid, "");
            if (success) {
                showMessage("Connected to " + ssid);
                updateConnectionStatus();
            } else {
                showError("Failed to connect to " + ssid);
            }
        }
    }
}

void ScreenNetwork::onWifiDisconnect() {
    if (m_bridge && m_bridge->getNetworkManager()) {
        m_bridge->getNetworkManager()->disconnectWifi();
        showMessage("Disconnected from WiFi");
        updateConnectionStatus();
    }
}

void ScreenNetwork::showPasswordDialog(const std::string& ssid) {
    // Create password input dialog
    // This is a simplified implementation
    showMessage("Password dialog for " + ssid + " would be shown here", "WiFi Password");
    
    // In a real implementation, you would create a proper password input dialog
    // For now, just try to connect with a common password or prompt via different method
}

void ScreenNetwork::onServiceToggle(const std::string& service, bool enabled) {
    if (!m_bridge) {
        return;
    }
    
    auto networkManager = m_bridge->getNetworkManager();
    if (!networkManager) {
        return;
    }
    
    if (service == "smb") {
        auto smbServer = networkManager->getSmbServer();
        if (smbServer) {
            if (enabled) {
                smbServer->start();
            } else {
                smbServer->stop();
            }
        }
    } else if (service == "http") {
        auto httpServer = networkManager->getHttpServer();
        if (httpServer) {
            if (enabled) {
                httpServer->start();
            } else {
                httpServer->stop();
            }
        }
    }
    
    LOG_INFO(service + " service " + (enabled ? "enabled" : "disabled"), "NETWORK_GUI");
}