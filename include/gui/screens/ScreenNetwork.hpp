#pragma once

#include "../Screen.hpp"
#include "../../network/NetworkManager.hpp"
#include <vector>

class ScreenNetwork : public Screen {
public:
    ScreenNetwork(const std::string& name, UsbBridge* bridge);
    
    bool create() override;
    void show() override;
    void update() override;

private:
    void createWifiSection();
    void createEthernetSection();
    void createServiceStatus();
    void scanWifiNetworks();
    void updateWifiList();
    void updateConnectionStatus();
    void onWifiConnect(const std::string& ssid);
    void onWifiDisconnect();
    void showPasswordDialog(const std::string& ssid);
    void onServiceToggle(const std::string& service, bool enabled);
    
    lv_obj_t* m_wifiList;
    lv_obj_t* m_scanButton;
    lv_obj_t* m_connectionStatus;
    lv_obj_t* m_ethernetStatus;
    lv_obj_t* m_smbSwitch;
    lv_obj_t* m_httpSwitch;
    lv_obj_t* m_passwordDialog;
    
    std::vector<WifiNetwork> m_wifiNetworks;
    std::string m_selectedSsid;
};
