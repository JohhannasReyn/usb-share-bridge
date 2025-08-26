#include "gui/screens/ScreenLogViewer.hpp"
#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"

ScreenLogViewer::ScreenLogViewer(const std::string& name, UsbBridge* bridge)
    : Screen(name, bridge)
    , m_currentFilter("all")
{
}

bool ScreenLogViewer::create() {
    m_container = lv_obj_create(nullptr);
    lv_obj_set_size(m_container, 480, 290);
    lv_obj_set_pos(m_container, 0, 30);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xFFFFFF), 0);
    
    // Title and controls
    createControls();
    
    // Log list
    m_logList = lv_list_create(m_container);
    lv_obj_set_size(m_logList, 460, 200);
    lv_obj_set_pos(m_logList, 10, 60);
    
    return true;
}

void ScreenLogViewer::createControls() {
    // Title
    lv_obj_t* title = lv_label_create(m_container);
    lv_label_set_text(title, "Activity Log");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title, 10, 10);
    
    // Filter dropdown
    m_filterDropdown = lv_dropdown_create(m_container);
    lv_dropdown_set_options(m_filterDropdown, "All\nCreated\nModified\nDeleted\nMoved");
    lv_obj_set_size(m_filterDropdown, 100, 30);
    lv_obj_set_pos(m_filterDropdown, 120, 10);
    lv_obj_add_event_cb(m_filterDropdown, [](lv_event_t* e) {
        ScreenLogViewer* screen = static_cast<ScreenLogViewer*>(lv_event_get_user_data(e));
        screen->onFilterChanged();
    }, LV_EVENT_VALUE_CHANGED, this);
    
    // Count label
    m_countLabel = lv_label_create(m_container);
    lv_obj_set_pos(m_countLabel, 240, 15);
    lv_label_set_text(m_countLabel, "0 events");
    
    // Clear button
    m_clearButton = lv_btn_create(m_container);
    lv_obj_set_size(m_clearButton, 60, 30);
    lv_obj_set_pos(m_clearButton, 350, 10);
    lv_obj_add_event_cb(m_clearButton, [](lv_event_t* e) {
        ScreenLogViewer* screen = static_cast<ScreenLogViewer*>(lv_event_get_user_data(e));
        screen->clearLogs();
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* clearLabel = lv_label_create(m_clearButton);
    lv_label_set_text(clearLabel, "Clear");
    lv_obj_center(clearLabel);
    
    // Home button
    lv_obj_t* homeButton = lv_btn_create(m_container);
    lv_obj_set_size(homeButton, 50, 30);
    lv_obj_set_pos(homeButton, 420, 10);
    lv_obj_add_event_cb(homeButton, [](lv_event_t* e) {
        ScreenLogViewer* screen = static_cast<ScreenLogViewer*>(lv_event_get_user_data(e));
        screen->navigateToScreen("home");
    }, LV_EVENT_CLICKED, this);
    
    lv_obj_t* homeLabel = lv_label_create(homeButton);
    lv_label_set_text(homeLabel, LV_SYMBOL_HOME);
    lv_obj_center(homeLabel);
}

void ScreenLogViewer::show() {
    Screen::show();
    refreshLogs();
}

void ScreenLogViewer::update() {
    // Refresh logs periodically
    static uint32_t lastRefresh = 0;
    uint32_t now = lv_tick_get();
    if (now - lastRefresh > 5000) { // Refresh every 5 seconds
        refreshLogs();
        lastRefresh = now;
    }
}

void ScreenLogViewer::refreshLogs() {
    if (!m_bridge || !m_bridge->getFileLogger()) {
        return;
    }
    
    auto allEvents = m_bridge->getFileLogger()->getRecentEvents(100);
    m_displayedEvents.clear();
    
    // Apply filter
    for (const auto& event : allEvents) {
        if (m_currentFilter == "all" ||
            (m_currentFilter == "created" && event.type == FileChangeEvent::CREATED) ||
            (m_currentFilter == "modified" && event.type == FileChangeEvent::MODIFIED) ||
            (m_currentFilter == "deleted" && event.type == FileChangeEvent::DELETED) ||
            (m_currentFilter == "moved" && event.type == FileChangeEvent::MOVED)) {
            m_displayedEvents.push_back(event);
        }
    }
    
    updateLogDisplay();
}

void ScreenLogViewer::updateLogDisplay() {
    // Clear existing items
    lv_obj_clean(m_logList);
    
    // Add filtered events
    for (const auto& event : m_displayedEvents) {
        lv_obj_t* item = lv_list_add_text(m_logList, "");
        
        std::string typeStr;
        switch (event.type) {
            case FileChangeEvent::CREATED: typeStr = "CREATE"; break;
            case FileChangeEvent::MODIFIED: typeStr = "MODIFY"; break;
            case FileChangeEvent::DELETED: typeStr = "DELETE"; break;
            case FileChangeEvent::MOVED: typeStr = "MOVE"; break;
        }
        
        auto time = std::chrono::system_clock::to_time_t(event.timestamp);
        struct tm* timeinfo = localtime(&time);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
        
        std::string text = std::string(timeStr) + " " + typeStr + " " + event.path;
        if (event.type == FileChangeEvent::MOVED && !event.oldPath.empty()) {
            text += " -> " + event.oldPath;
        }
        
        lv_label_set_text(lv_obj_get_child(item, 0), text.c_str());
    }
    
    // Update count
    std::string countText = std::to_string(m_displayedEvents.size()) + " events";
    lv_label_set_text(m_countLabel, countText.c_str());
}

void ScreenLogViewer::onFilterChanged() {
    uint16_t selected = lv_dropdown_get_selected(m_filterDropdown);
    const char* filters[] = {"all", "created", "modified", "deleted", "moved"};
    
    if (selected < 5) {
        m_currentFilter = filters[selected];
        refreshLogs();
    }
}

void ScreenLogViewer::clearLogs() {
    if (m_bridge && m_bridge->getFileLogger()) {
        auto now = std::chrono::system_clock::now();
        m_bridge->getFileLogger()->clearOldEvents(now);
        refreshLogs();
    }
}
