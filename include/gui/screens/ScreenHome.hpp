#pragma once

#include "../Screen.hpp"
#include "../Widgets.hpp"
#include <memory>

class ScreenHome : public Screen {
public:
    ScreenHome(const std::string& name, UsbBridge* bridge);
    
    bool create() override;
    void update() override;

private:
    void createNavigationButtons();
    
    std::unique_ptr<StatusWidget> m_statusWidget;
};