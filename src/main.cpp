#include <iostream>
#include <signal.h>
#include <unistd.h>
#include "../include/core/UsbBridge.hpp"
#include "../include/gui/GuiManager.hpp"
#include "../include/core/ConfigManager.hpp"
#include "../include/utils/Logger.hpp"

std::unique_ptr<UsbBridge> g_bridge;
std::unique_ptr<GuiManager> g_gui;
volatile bool g_running = true;

void signalHandler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal), "MAIN");
    g_running = false;
}

int main() {
    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Initialize logging
    Logger::instance().setLogFile("/data/logs/system.log");
    Logger::instance().setLogLevel(LogLevel::INFO);
    LOG_INFO("USB Bridge starting up...", "MAIN");
    
    try {
        // Load configuration
        if (!ConfigManager::instance().loadConfig()) {
            LOG_WARNING("Failed to load configuration, using defaults", "MAIN");
        }
        
        // Create and initialize the USB bridge
        g_bridge = std::make_unique<UsbBridge>();
        if (!g_bridge->initialize()) {
            LOG_FATAL("Failed to initialize USB Bridge", "MAIN");
            return 1;
        }
        
        // Create and initialize the GUI
        g_gui = std::make_unique<GuiManager>(g_bridge.get());
        if (!g_gui->initialize()) {
            LOG_FATAL("Failed to initialize GUI", "MAIN");
            return 1;
        }
        
        LOG_INFO("USB Bridge initialized successfully", "MAIN");
        
        // Start the bridge
        g_bridge->start();
        
        // Main loop
        while (g_running) {
            g_gui->update();
            usleep(10000); // 10ms delay for 100Hz update rate
        }
        
        LOG_INFO("Shutting down USB Bridge...", "MAIN");
        
    } catch (const std::exception& e) {
        LOG_FATAL("Unhandled exception: " + std::string(e.what()), "MAIN");
        return 1;
    }
    
    // Cleanup
    if (g_bridge) {
        g_bridge->stop();
    }
    if (g_gui) {
        g_gui->cleanup();
    }
    
    // Save configuration
    ConfigManager::instance().saveConfig();
    
    LOG_INFO("USB Bridge shutdown complete", "MAIN");
    return 0;
}
