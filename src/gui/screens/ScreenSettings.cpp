#include "gui/screens/ScreenSettings.hpp"
#include "core/UsbBridge.hpp"
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"

ScreenSettings::ScreenSettings(const std::string& name, UsbBridge* bridge)
    : Screen(name, bridge)
{
}

bool ScreenSettings::create() {
    m_container = lv_obj_create(nullptr);
    lv_obj_set_size(m_container, 480, 290);
    lv_obj_set_pos(m_container, 0, 30);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xFFFFFF), 0);
    
    // Title and home button
    lv_obj_t* title = lv_label_create(m_container);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title, 10, 10);
    
    lv_obj_t* homeButton = lv_btn_create(m_container);
    lv_obj_set_size(homeButton, 50, 30);
    lv_obj_set_pos(homeButton, 420, 10);
    lv_obj_add_event_cb(homeButton, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        screen->navigateToScreen("home");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* homeLabel = lv_label_create(homeButton);
    lv_label_set_text(homeLabel, LV_SYMBOL_HOME);
    lv_obj_center(homeLabel);
    
    // Settings container with scroll
    m_settingsContainer = lv_obj_create(m_container);
    lv_obj_set_size(m_settingsContainer, 460, 200);
    lv_obj_set_pos(m_settingsContainer, 10, 50);
    lv_obj_set_style_bg_color(m_settingsContainer, lv_color_hex(0xF8F8F8), 0);
    
    createUsbSettings();
    createNetworkSettings();
    createSystemSettings();
    
    // Save button
    m_saveButton = lv_btn_create(m_container);
    lv_obj_set_size(m_saveButton, 100, 30);
    lv_obj_set_pos(m_saveButton, 190, 260);
    lv_obj_add_event_cb(m_saveButton, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        screen->saveSettings();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* saveLabel = lv_label_create(m_saveButton);
    lv_label_set_text(saveLabel, "Save");
    lv_obj_center(saveLabel);
    
    return true;
}

void ScreenSettings::createUsbSettings() {
    int yPos = 10;
    
    // USB section header
    lv_obj_t* usbHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(usbHeader, "USB Hosts");
    lv_obj_set_style_text_font(usbHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(usbHeader, 10, yPos);
    yPos += 30;
    
    // USB Host 1
    lv_obj_t* host1Label = lv_label_create(m_settingsContainer);
    lv_label_set_text(host1Label, "USB Host 1:");
    lv_obj_set_pos(host1Label, 20, yPos);
    
    m_usbHost1Switch = lv_switch_create(m_settingsContainer);
    lv_obj_set_pos(m_usbHost1Switch, 120, yPos);
    lv_obj_add_event_cb(m_usbHost1Switch, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(screen->m_usbHost1Switch, LV_STATE_CHECKED);
        screen->onUsbHostToggle(0, enabled);
    }, LV_EVENT_VALUE_CHANGED, this);
    yPos += 40;
    
    // USB Host 2
    lv_obj_t* host2Label = lv_label_create(m_settingsContainer);
    lv_label_set_text(host2Label, "USB Host 2:");
    lv_obj_set_pos(host2Label, 20, yPos);
    
    m_usbHost2Switch = lv_switch_create(m_settingsContainer);
    lv_obj_set_pos(m_usbHost2Switch, 120, yPos);
    lv_obj_add_event_cb(m_usbHost2Switch, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(screen->m_usbHost2Switch, LV_STATE_CHECKED);
        screen->onUsbHostToggle(1, enabled);
    }, LV_EVENT_VALUE_CHANGED, this);
}

void ScreenSettings::createNetworkSettings() {
    int yPos = 100;
    
    // Network section header
    lv_obj_t* networkHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(networkHeader, "Network Sharing");
    lv_obj_set_style_text_font(networkHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(networkHeader, 10, yPos);
    yPos += 30;
    
    // Network sharing toggle
    lv_obj_t* networkLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(networkLabel, "Enable Network:");
    lv_obj_set_pos(networkLabel, 20, yPos);
    
    m_networkSwitch = lv_switch_create(m_settingsContainer);
    lv_obj_set_pos(m_networkSwitch, 150, yPos);
    lv_obj_add_event_cb(m_networkSwitch, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(screen->m_networkSwitch, LV_STATE_CHECKED);
        screen->onNetworkToggle(enabled);
    }, LV_EVENT_VALUE_CHANGED, this);
}

void ScreenSettings::createSystemSettings() {
    int yPos = 150;
    
    // System section header
    lv_obj_t* systemHeader = lv_label_create(m_settingsContainer);
    lv_label_set_text(systemHeader, "System");
    lv_obj_set_style_text_font(systemHeader, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(systemHeader, 250, 10);
    
    // Brightness control
    lv_obj_t* brightnessLabel = lv_label_create(m_settingsContainer);
    lv_label_set_text(brightnessLabel, "Brightness:");
    lv_obj_set_pos(brightnessLabel, 260, 40);
    
    m_brightnessSlider = lv_slider_create(m_settingsContainer);
    lv_obj_set_size(m_brightnessSlider, 150, 20);
    lv_obj_set_pos(m_brightnessSlider, 260, 70);
    lv_slider_set_range(m_brightnessSlider, 10, 100);
    
    // Factory reset button
    lv_obj_t* resetButton = lv_btn_create(m_settingsContainer);
    lv_obj_set_size(resetButton, 120, 30);
    lv_obj_set_pos(resetButton, 260, 110);
    lv_obj_set_style_bg_color(resetButton, lv_color_hex(0xFF5722), 0);
    lv_obj_add_event_cb(resetButton, [](lv_event_t* e) {
        ScreenSettings* screen = static_cast<ScreenSettings*>(lv_event_get_user_data(e));
        screen->onFactoryReset();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* resetLabel = lv_label_create(resetButton);
    lv_label_set_text(resetLabel, "Factory Reset");
    lv_obj_center(resetLabel);
}

void ScreenSettings::show() {
    Screen::show();
    loadSettings();
}

void ScreenSettings::loadSettings() {
    auto& config = ConfigManager::instance();
    
    // Load USB host settings
    bool host1Enabled = config.getBoolValue("usb.host1.enabled", true);
    bool host2Enabled = config.getBoolValue("usb.host2.enabled", true);
    
    if (host1Enabled) {
        lv_obj_add_state(m_usbHost1Switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(m_usbHost1Switch, LV_STATE_CHECKED);
    }
    
    if (host2Enabled) {
        lv_obj_add_state(m_usbHost2Switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(m_usbHost2Switch, LV_STATE_CHECKED);
    }
    
    // Load network settings
    bool networkEnabled = config.getBoolValue("network.enabled", false);
    if (networkEnabled) {
        lv_obj_add_state(m_networkSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(m_networkSwitch, LV_STATE_CHECKED);
    }
    
    // Load display brightness
    int brightness = config.getIntValue("display.brightness", 80);
    lv_slider_set_value(m_brightnessSlider, brightness, LV_ANIM_OFF);
}

void ScreenSettings::saveSettings() {
    auto& config = ConfigManager::instance();
    
    // Save USB host settings
    bool host1Enabled = lv_obj_has_state(m_usbHost1Switch, LV_STATE_CHECKED);
    bool host2Enabled = lv_obj_has_state(m_usbHost2Switch, LV_STATE_CHECKED);
    
    config.setValue("usb.host1.enabled", host1Enabled);
    config.setValue("usb.host2.enabled", host2Enabled);
    
    // Save network settings
    bool networkEnabled = lv_obj_has_state(m_networkSwitch, LV_STATE_CHECKED);
    config.setValue("network.enabled", networkEnabled);
    
    // Save brightness
    int brightness = lv_slider_get_value(m_brightnessSlider);
    config.setValue("display.brightness", brightness);
    
    // Save configuration to file
    if (config.saveConfig()) {
        showMessage("Settings saved successfully");
        LOG_INFO("Settings saved", "SETTINGS");
    } else {
        showError("Failed to save settings");
        LOG_ERROR("Failed to save settings", "SETTINGS");
    }
}

void ScreenSettings::onUsbHostToggle(int hostId, bool enabled) {
    if (!m_bridge) return;
    
    if (enabled) {
        m_bridge->connectUsbHost(hostId);
    } else {
        m_bridge->disconnectUsbHost(hostId);
    }
    
    LOG_INFO("USB Host " + std::to_string(hostId) + " " + (enabled ? "enabled" : "disabled"), "SETTINGS");
}

void ScreenSettings::onNetworkToggle(bool enabled) {
    if (!m_bridge) return;
    
    if (enabled) {
        m_bridge->enableNetworkSharing();
    } else {
        m_bridge->disableNetworkSharing();
    }
    
    LOG_INFO("Network sharing " + (enabled ? "enabled" : "disabled"), "SETTINGS");
}

void ScreenSettings::onFactoryReset() {
    // Show confirmation dialog
    showMessage("This will reset all settings to defaults. Please restart the device after confirmation.", "Factory Reset");
    
    // Reset configuration
    auto& config = ConfigManager::instance();
    config.setValue("usb.host1.enabled", true);
    config.setValue("usb.host2.enabled", true);
    config.setValue("network.enabled", false);
    config.setValue("display.brightness", 80);
    
    if (config.saveConfig()) {
        LOG_INFO("Factory reset completed", "SETTINGS");
        loadSettings(); // Refresh UI
    }
}