#pragma once

#include <vector>
#include <thread>
#include <atomic>

enum class LedColor {
    RED,
    GREEN,
    BLUE,
    YELLOW,
    PURPLE,
    CYAN,
    WHITE,
    OFF
};

enum class LedPattern {
    SOLID,
    BLINK_SLOW,
    BLINK_FAST,
    PULSE,
    FADE,
    RAINBOW
};

class LedController {
public:
    LedController();
    ~LedController();

    bool initialize();
    void cleanup();
    
    // Status LEDs (assuming RGB LED for status indication)
    void setStatusLed(LedColor color, LedPattern pattern = LedPattern::SOLID);
    void setUsbStatusLed(bool connected);
    void setNetworkStatusLed(bool connected);
    void setActivityLed(bool active);
    
    // Custom LED control
    void setLed(int ledIndex, int red, int green, int blue);
    void setLedPattern(int ledIndex, LedPattern pattern, LedColor color);
    
    // Brightness control
    void setBrightness(int brightness); // 0-100
    int getBrightness() const { return m_brightness; }

private:
    void updateLoop();
    void updatePattern(int ledIndex);
    void applyColor(int ledIndex, LedColor color, int brightness = 100);
    
    struct LedState {
        LedColor color;
        LedPattern pattern;
        int currentBrightness;
        uint64_t lastUpdate;
        int patternStep;
    };
    
    std::vector<LedState> m_leds;
    std::atomic<bool> m_running;
    std::thread m_updateThread;
    int m_brightness;
    bool m_initialized;
    
    // GPIO pins for RGB LED (adjust for your board)
    static const int RED_PIN = 12;
    static const int GREEN_PIN = 13;
    static const int BLUE_PIN = 19;
};
