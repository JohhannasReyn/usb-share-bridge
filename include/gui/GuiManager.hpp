#pragma once

#include <memory>
#include <vector>
#include <functional>
#include "lvgl.h"
#include "Screen.hpp"
#include "../hardware/DisplayDriver.hpp"
#include "../hardware/TouchDriver.hpp"

class UsbBridge; // Forward declaration

class GuiManager {
public:
    GuiManager(UsbBridge* bridge);
    ~GuiManager();

    bool initialize();
    void cleanup();
    
    // Screen management
    void showScreen(const std::string& screenName);
    void registerScreen(const std::string& name, std::unique_ptr<Screen> screen);
    Screen* getCurrentScreen() const { return m_currentScreen.get(); }
    
    // Main UI loop
    void update();
    void refresh();
    
    // Theme and styling
    void setTheme(const std::string& themeName);
    void updateStatusBar();
    
    // Event handling
    void handleTouchEvent(int x, int y, bool pressed);
    void handleButtonPress(int buttonId);

private:
    void setupLVGL();
    void createStatusBar();
    void updateConnectionStatus();
    void updateStorageInfo();
    
    UsbBridge* m_bridge;
    std::unique_ptr<DisplayDriver> m_displayDriver;
    std::unique_ptr<TouchDriver> m_touchDriver;
    
    std::map<std::string, std::unique_ptr<Screen>> m_screens;
    std::unique_ptr<Screen> m_currentScreen;
    
    lv_obj_t* m_statusBar;
    lv_obj_t* m_usbStatusIcon;
    lv_obj_t* m_wifiStatusIcon;
    lv_obj_t* m_storageIcon;
    lv_obj_t* m_timeLabel;
    
    bool m_initialized;
};
