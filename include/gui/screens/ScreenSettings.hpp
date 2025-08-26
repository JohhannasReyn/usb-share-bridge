#pragma once

#include "../Screen.hpp"

class ScreenSettings : public Screen {
public:
    ScreenSettings(const std::string& name, UsbBridge* bridge);
    
    bool create() override;
    void show() override;

private:
    void createUsbSettings();
    void createNetworkSettings();
    void createSystemSettings();
    void saveSettings();
    void loadSettings();
    void onUsbHostToggle(int hostId, bool enabled);
    void onNetworkToggle(bool enabled);
    void onFactoryReset();
    
    lv_obj_t* m_settingsContainer;
    lv_obj_t* m_usbHost1Switch;
    lv_obj_t* m_usbHost2Switch;
    lv_obj_t* m_networkSwitch;
    lv_obj_t* m_brightnessSlider;
    lv_obj_t* m_saveButton;
};
