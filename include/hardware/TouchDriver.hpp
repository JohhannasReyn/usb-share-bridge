#pragma once

#include <functional>
#include <thread>
#include <atomic>

struct TouchPoint {
    int x, y;
    bool pressed;
    int pressure;
    uint64_t timestamp;
};

class TouchDriver {
public:
    TouchDriver();
    ~TouchDriver();

    bool initialize(int i2cBus = 1, int i2cAddress = 0x38); // Common for capacitive touch
    void cleanup();
    
    // Touch callbacks
    void setTouchCallback(std::function<void(const TouchPoint&)> callback);
    
    // Calibration
    void calibrate();
    void loadCalibration();
    void saveCalibration();
    
    // Configuration
    void setSensitivity(int level); // 1-10
    void setDebounceTime(int ms);
    
    bool isInitialized() const { return m_initialized; }

private:
    void touchLoop();
    TouchPoint readTouch();
    TouchPoint applyCalibration(const TouchPoint& raw);
    
    int m_i2cDevice;
    bool m_initialized;
    std::atomic<bool> m_running;
    std::thread m_touchThread;
    
    std::function<void(const TouchPoint&)> m_touchCallback;
    
    // Calibration data
    struct {
        int xMin, xMax, yMin, yMax;
        int xOffset, yOffset;
        float xScale, yScale;
    } m_calibration;
    
    int m_sensitivity;
    int m_debounceTime;
    TouchPoint m_lastTouch;
};
