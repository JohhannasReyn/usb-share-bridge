#pragma once

#include "../Screen.hpp"
#include "../../core/FileChangeLogger.hpp"
#include <vector>

class ScreenLogViewer : public Screen {
public:
    ScreenLogViewer(const std::string& name, UsbBridge* bridge);
    
    bool create() override;
    void show() override;
    void update() override;

private:
    void createControls();
    void refreshLogs();
    void updateLogDisplay();
    void onFilterChanged();
    void clearLogs();
    
    lv_obj_t* m_logList;
    lv_obj_t* m_filterDropdown;
    lv_obj_t* m_countLabel;
    lv_obj_t* m_clearButton;
    
    std::vector<FileChangeEvent> m_displayedEvents;
    std::string m_currentFilter;
};
