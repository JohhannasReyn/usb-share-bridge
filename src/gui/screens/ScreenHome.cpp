#include "gui/screens/ScreenHome.hpp"
#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"

ScreenHome::ScreenHome(const std::string& name, UsbBridge* bridge) 
    : Screen(name, bridge) 
{
}

bool ScreenHome::create() {
    m_container = lv_obj_create(nullptr);
    lv_obj_set_size(m_container, 480, 290); // Full screen minus status bar
    lv_obj_set_pos(m_container, 0, 30);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xF5F5F5), 0);
    lv_obj_clear_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t* title = lv_label_create(m_container);
    lv_label_set_text(title, "USB Bridge");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_center(title);
    lv_obj_set_pos(title, 0, -80);
    
    // Status widget
    m_statusWidget = std::make_unique<StatusWidget>(m_container);
    lv_obj_set_pos(m_statusWidget->getWidget(), 20, 80);
    
    // Navigation buttons
    createNavigationButtons();
    
    return true;
}

void ScreenHome::createNavigationButtons() {
    // File Explorer button
    lv_obj_t* btnFiles = lv_btn_create(m_container);
    lv_obj_set_size(btnFiles, 100, 60);
    lv_obj_set_pos(btnFiles, 50, 180);
    lv_obj_add_event_cb(btnFiles, [](lv_event_t* e) {
        ScreenHome* screen = static_cast<ScreenHome*>(lv_event_get_user_data(e));
        screen->navigateToScreen("files");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* labelFiles = lv_label_create(btnFiles);
    lv_label_set_text(labelFiles, LV_SYMBOL_DIRECTORY "\nFiles");
    lv_obj_center(labelFiles);
    
    // Settings button
    lv_obj_t* btnSettings = lv_btn_create(m_container);
    lv_obj_set_size(btnSettings, 100, 60);
    lv_obj_set_pos(btnSettings, 170, 180);
    lv_obj_add_event_cb(btnSettings, [](lv_event_t* e) {
        ScreenHome* screen = static_cast<ScreenHome*>(lv_event_get_user_data(e));
        screen->navigateToScreen("settings");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* labelSettings = lv_label_create(btnSettings);
    lv_label_set_text(labelSettings, LV_SYMBOL_SETTINGS "\nSettings");
    lv_obj_center(labelSettings);
    
    // Network button
    lv_obj_t* btnNetwork = lv_btn_create(m_container);
    lv_obj_set_size(btnNetwork, 100, 60);
    lv_obj_set_pos(btnNetwork, 290, 180);
    lv_obj_add_event_cb(btnNetwork, [](lv_event_t* e) {
        ScreenHome* screen = static_cast<ScreenHome*>(lv_event_get_user_data(e));
        screen->navigateToScreen("network");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* labelNetwork = lv_label_create(btnNetwork);
    lv_label_set_text(labelNetwork, LV_SYMBOL_WIFI "\nNetwork");
    lv_obj_center(labelNetwork);
}

void ScreenHome::update() {
    if (!m_visible || !m_bridge) return;
    
    // Update status widget
    auto connectedHosts = m_bridge->getConnectedHosts();
    m_statusWidget->setUsbStatus(!connectedHosts.empty(), connectedHosts.size());
    m_statusWidget->setNetworkStatus(m_bridge->isNetworkActive());
    
    if (m_bridge->getStorageManager()->isDriveConnected()) {
        auto driveInfo = m_bridge->getStorageManager()->getDriveInfo();
        m_statusWidget->setStorageStatus(true, driveInfo.freeSpace, driveInfo.totalSpace);
    } else {
        m_statusWidget->setStorageStatus(false);
    }
}
