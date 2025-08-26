#pragma once

#include "lvgl.h"
#include <string>
#include <functional>

class UsbBridge; // Forward declaration

class Screen {
public:
    Screen(const std::string& name, UsbBridge* bridge);
    virtual ~Screen();

    virtual bool create() = 0;
    virtual void destroy();
    virtual void show();
    virtual void hide();
    virtual void update() {}
    
    const std::string& getName() const { return m_name; }
    lv_obj_t* getContainer() const { return m_container; }
    
    // Navigation callbacks
    void setNavigationCallback(std::function<void(const std::string&)> callback);

protected:
    void navigateToScreen(const std::string& screenName);
    void showMessage(const std::string& message, const std::string& title = "Info");
    void showError(const std::string& error);
    
    std::string m_name;
    UsbBridge* m_bridge;
    lv_obj_t* m_container;
    std::function<void(const std::string&)> m_navigationCallback;
    bool m_visible;
};
