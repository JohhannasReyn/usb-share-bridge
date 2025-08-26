#include "gui/GuiManager.hpp"
#include "gui/screens/ScreenHome.hpp"
#include "gui/screens/ScreenFileExplorer.hpp"
#include "gui/screens/ScreenLogViewer.hpp"
#include "gui/screens/ScreenSettings.hpp"
#include "gui/screens/ScreenNetwork.hpp"
#include "core/UsbBridge.hpp"
#include "utils/Logger.hpp"
#include <ctime>

GuiManager::GuiManager(UsbBridge* bridge) 
    : m_bridge(bridge)
    , m_statusBar(nullptr)
    , m_initialized(false)
{
}

GuiManager::~GuiManager() {
    cleanup();
}

bool GuiManager::initialize() {
    LOG_INFO("Initializing GUI Manager", "GUI");
    
    try {
        // Initialize display driver
        m_displayDriver = std::make_unique<DisplayDriver>();
        if (!m_displayDriver->initialize()) {
            LOG_ERROR("Failed to initialize display driver", "GUI");
            return false;
        }
        
        // Initialize touch driver
        m_touchDriver = std::make_unique<TouchDriver>();
        if (!m_touchDriver->initialize()) {
            LOG_WARNING("Failed to initialize touch driver", "GUI");
            // Continue without touch - not critical for basic operation
        }
        
        // Setup LVGL
        setupLVGL();
        
        // Create status bar
        createStatusBar();
        
        // Register screens
        registerScreen("home", std::make_unique<ScreenHome>("home", m_bridge));
        registerScreen("files", std::make_unique<ScreenFileExplorer>("files", m_bridge));
        registerScreen("logs", std::make_unique<ScreenLogViewer>("logs", m_bridge));
        registerScreen("settings", std::make_unique<ScreenSettings>("settings", m_bridge));
        registerScreen("network", std::make_unique<ScreenNetwork>("network", m_bridge));
        
        // Set navigation callbacks
        for (auto& [name, screen] : m_screens) {
            screen->setNavigationCallback([this](const std::string& screenName) {
                showScreen(screenName);
            });
        }
        
        // Show home screen
        showScreen("home");
        
        // Setup touch callback
        if (m_touchDriver) {
            m_touchDriver->setTouchCallback([this](const TouchPoint& point) {
                handleTouchEvent(point.x, point.y, point.pressed);
            });
        }
        
        m_initialized = true;
        LOG_INFO("GUI Manager initialized successfully", "GUI");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during GUI initialization: " + std::string(e.what()), "GUI");
        return false;
    }
}

void GuiManager::cleanup() {
    if (!m_initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up GUI Manager", "GUI");
    
    // Cleanup screens
    m_currentScreen.reset();
    m_screens.clear();
    
    // Cleanup LVGL
    lv_deinit();
    
    // Cleanup drivers
    m_touchDriver.reset();
    m_displayDriver.reset();
    
    m_initialized = false;
}

void GuiManager::setupLVGL() {
    // Initialize LVGL
    lv_init();
    
    // Create display buffer
    static lv_color_t buf1[480 * 320 / 10];
    static lv_color_t buf2[480 * 320 / 10];
    static lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, buf1, buf2, sizeof(buf1));
    
    // Create display driver
    lv_display_t* disp = lv_display_create(480, 320);
    lv_display_set_draw_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, [](lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
        // This would be connected to the actual display driver
        lv_display_flush_ready(disp);
    });
    
    // Create input device for touch
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, [](lv_indev_t* indev, lv_indev_data_t* data) {
        // Touch input will be handled through callbacks
        data->state = LV_INDEV_STATE_RELEASED;
    });
    
    // Set default theme
    lv_theme_t* theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), 
                                              lv_palette_main(LV_PALETTE_RED), 
                                              true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
}

void GuiManager::createStatusBar() {
    m_statusBar = lv_obj_create(lv_screen_active());
    lv_obj_set_size(m_statusBar, 480, 30);
    lv_obj_set_pos(m_statusBar, 0, 0);
    lv_obj_set_style_bg_color(m_statusBar, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(m_statusBar, 0, 0);
    lv_obj_clear_flag(m_statusBar, LV_OBJ_FLAG_SCROLLABLE);
    
    // USB status icon
    m_usbStatusIcon = lv_label_create(m_statusBar);
    lv_label_set_text(m_usbStatusIcon, LV_SYMBOL_USB);
    lv_obj_set_pos(m_usbStatusIcon, 5, 5);
    lv_obj_set_style_text_color(m_usbStatusIcon, lv_color_white(), 0);
    
    // WiFi status icon
    m_wifiStatusIcon = lv_label_create(m_statusBar);
    lv_label_set_text(m_wifiStatusIcon, LV_SYMBOL_WIFI);
    lv_obj_set_pos(m_wifiStatusIcon, 35, 5);
    lv_obj_set_style_text_color(m_wifiStatusIcon, lv_color_white(), 0);
    
    // Storage icon
    m_storageIcon = lv_label_create(m_statusBar);
    lv_label_set_text(m_storageIcon, LV_SYMBOL_SD_CARD);
    lv_obj_set_pos(m_storageIcon, 65, 5);
    lv_obj_set_style_text_color(m_storageIcon, lv_color_white(), 0);
    
    // Time label
    m_timeLabel = lv_label_create(m_statusBar);
    lv_obj_set_pos(m_timeLabel, 380, 5);
    lv_obj_set_style_text_color(m_timeLabel, lv_color_white(), 0);
    updateStatusBar();
}

void GuiManager::showScreen(const std::string& screenName) {
    auto it = m_screens.find(screenName);
    if (it == m_screens.end()) {
        LOG_ERROR("Screen not found: " + screenName, "GUI");
        return;
    }
    
    // Hide current screen
    if (m_currentScreen) {
        m_currentScreen->hide();
    }
    
    // Show new screen
    m_currentScreen = it->second.get();
    m_currentScreen->show();
    
    LOG_INFO("Switched to screen: " + screenName, "GUI");
}

void GuiManager::registerScreen(const std::string& name, std::unique_ptr<Screen> screen) {
    if (!screen->create()) {
        LOG_ERROR("Failed to create screen: " + name, "GUI");
        return;
    }
    m_screens[name] = std::move(screen);
}

void GuiManager::update() {
    if (!m_initialized) {
        return;
    }
    
    // Update LVGL
    lv_task_handler();
    
    // Update status bar periodically
    static uint32_t lastStatusUpdate = 0;
    uint32_t now = lv_tick_get();
    if (now - lastStatusUpdate > 1000) { // Update every second
        updateStatusBar();
        lastStatusUpdate = now;
    }
    
    // Update current screen
    if (m_currentScreen) {
        m_currentScreen->update();
    }
}

void GuiManager::updateStatusBar() {
    updateConnectionStatus();
    
    // Update time
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
    lv_label_set_text(m_timeLabel, buffer);
}

void GuiManager::updateConnectionStatus() {
    if (!m_bridge) return;
    
    // USB status
    auto connectedHosts = m_bridge->getConnectedHosts();
    if (!connectedHosts.empty()) {
        lv_obj_set_style_text_color(m_usbStatusIcon, lv_color_hex(0x4CAF50), 0); // Green
    } else {
        lv_obj_set_style_text_color(m_usbStatusIcon, lv_color_hex(0x757575), 0); // Gray
    }
    
    // Network status
    if (m_bridge->isNetworkActive()) {
        lv_obj_set_style_text_color(m_wifiStatusIcon, lv_color_hex(0x4CAF50), 0); // Green
    } else {
        lv_obj_set_style_text_color(m_wifiStatusIcon, lv_color_hex(0x757575), 0); // Gray
    }
    
    // Storage status
    if (m_bridge->getStorageManager()->isDriveConnected()) {
        lv_obj_set_style_text_color(m_storageIcon, lv_color_hex(0x4CAF50), 0); // Green
    } else {
        lv_obj_set_style_text_color(m_storageIcon, lv_color_hex(0x757575), 0); // Gray
    }
}

void GuiManager::handleTouchEvent(int x, int y, bool pressed) {
    // Convert touch coordinates and send to LVGL
    lv_indev_data_t data;
    data.point.x = x;
    data.point.y = y;
    data.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    
    // This would normally be handled by LVGL's input driver
    // For now, we'll just log the event
    if (pressed) {
        LOG_DEBUG("Touch event at (" + std::to_string(x) + ", " + std::to_string(y) + ")", "GUI");
    }
}
