#pragma once

#include <cstdint>
#include <string>

// Hardware abstraction for BigTechTree Pi V1.2.1 TFT display
class DisplayDriver {
public:
    struct DisplayConfig {
        int width = 480;      // 4.0" TFT typical resolution
        int height = 320;
        int colorDepth = 16;  // RGB565
        int spiSpeed = 40000000; // 40MHz
        int backlightPin = 18;   // GPIO for backlight control
        int resetPin = 22;       // GPIO for display reset
        int dcPin = 24;          // Data/Command pin
        int csPin = 8;           // Chip select
    };

    DisplayDriver();
    ~DisplayDriver();

    bool initialize(const DisplayConfig& config = DisplayConfig{});
    void cleanup();
    
    // Display control
    void setBacklight(int brightness); // 0-100
    int getBacklight() const { return m_backlight; }
    void turnOn();
    void turnOff();
    bool isOn() const { return m_displayOn; }
    
    // Buffer operations for LVGL
    void flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t* color_p);
    void setPixel(int x, int y, uint16_t color);
    
    // Display info
    int getWidth() const { return m_config.width; }
    int getHeight() const { return m_config.height; }
    int getColorDepth() const { return m_config.colorDepth; }

private:
    bool initializeSPI();
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void writeData16(uint16_t data);
    void setWindow(int x1, int y1, int x2, int y2);
    
    DisplayConfig m_config;
    int m_spiDevice;
    int m_backlight;
    bool m_initialized;
    bool m_displayOn;
};
