#include "hardware/TouchDriver.hpp"
#include "utils/Logger.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <fstream>
#include <chrono>

TouchDriver::TouchDriver()
    : m_i2cDevice(-1)
    , m_initialized(false)
    , m_running(false)
    , m_sensitivity(5)
    , m_debounceTime(50)
{
    // Default calibration values for 4" display
    m_calibration.xMin = 200;
    m_calibration.xMax = 3900;
    m_calibration.yMin = 200;
    m_calibration.yMax = 3900;
    m_calibration.xOffset = 0;
    m_calibration.yOffset = 0;
    m_calibration.xScale = 480.0f / (m_calibration.xMax - m_calibration.xMin);
    m_calibration.yScale = 320.0f / (m_calibration.yMax - m_calibration.yMin);
    
    m_lastTouch.x = 0;
    m_lastTouch.y = 0;
    m_lastTouch.pressed = false;
    m_lastTouch.pressure = 0;
    m_lastTouch.timestamp = 0;
}

TouchDriver::~TouchDriver() {
    cleanup();
}

bool TouchDriver::initialize(int i2cBus, int i2cAddress) {
    LOG_INFO("Initializing touch driver", "TOUCH");
    
    std::string devicePath = "/dev/i2c-" + std::to_string(i2cBus);
    m_i2cDevice = open(devicePath.c_str(), O_RDWR);
    
    if (m_i2cDevice < 0) {
        LOG_ERROR("Failed to open I2C device: " + devicePath, "TOUCH");
        return false;
    }
    
    if (ioctl(m_i2cDevice, I2C_SLAVE, i2cAddress) < 0) {
        LOG_ERROR("Failed to set I2C slave address", "TOUCH");
        close(m_i2cDevice);
        m_i2cDevice = -1;
        return false;
    }
    
    // Test communication with touch controller
    uint8_t testByte = 0;
    if (read(m_i2cDevice, &testByte, 1) < 0) {
        LOG_WARNING("Touch controller may not be responding", "TOUCH");
        // Continue anyway - some controllers need special initialization
    }
    
    // Load calibration if available
    loadCalibration();
    
    // Start touch monitoring thread
    m_running = true;
    m_touchThread = std::thread(&TouchDriver::touchLoop, this);
    
    m_initialized = true;
    LOG_INFO("Touch driver initialized successfully", "TOUCH");
    return true;
}

void TouchDriver::cleanup() {
    if (!m_initialized) {
        return;
    }
    
    m_running = false;
    
    if (m_touchThread.joinable()) {
        m_touchThread.join();
    }
    
    if (m_i2cDevice >= 0) {
        close(m_i2cDevice);
        m_i2cDevice = -1;
    }
    
    m_initialized = false;
}

void TouchDriver::setTouchCallback(std::function<void(const TouchPoint&)> callback) {
    m_touchCallback = callback;
}

void TouchDriver::calibrate() {
    LOG_INFO("Starting touch calibration", "TOUCH");
    
    // This is a simplified calibration - in a real implementation,
    // you would show calibration points and collect touch data
    
    struct CalibrationPoint {
        int screenX, screenY;
        int touchX, touchY;
    };
    
    CalibrationPoint points[4] = {
        {50, 50, 0, 0},      // Top-left
        {430, 50, 0, 0},     // Top-right
        {50, 270, 0, 0},     // Bottom-left
        {430, 270, 0, 0}     // Bottom-right
    };
    
    // For now, use default calibration values
    // In a real implementation, you would:
    // 1. Display calibration points
    // 2. Wait for user to touch each point
    // 3. Record raw touch coordinates
    // 4. Calculate calibration parameters
    
    saveCalibration();
    LOG_INFO("Touch calibration completed", "TOUCH");
}

void TouchDriver::loadCalibration() {
    std::ifstream file("/data/touch_calibration.dat", std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&m_calibration), sizeof(m_calibration));
        file.close();
        LOG_INFO("Touch calibration loaded", "TOUCH");
    } else {
        LOG_INFO("Using default touch calibration", "TOUCH");
    }
}

void TouchDriver::saveCalibration() {
    std::ofstream file("/data/touch_calibration.dat", std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&m_calibration), sizeof(m_calibration));
        file.close();
        LOG_INFO("Touch calibration saved", "TOUCH");
    } else {
        LOG_ERROR("Failed to save touch calibration", "TOUCH");
    }
}

void TouchDriver::setSensitivity(int level) {
    if (level >= 1 && level <= 10) {
        m_sensitivity = level;
    }
}

void TouchDriver::setDebounceTime(int ms) {
    if (ms >= 0 && ms <= 1000) {
        m_debounceTime = ms;
    }
}

void TouchDriver::touchLoop() {
    auto lastReadTime = std::chrono::steady_clock::now();
    
    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastRead = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReadTime);
        
        // Read touch data at ~100Hz
        if (timeSinceLastRead.count() >= 10) {
            TouchPoint rawTouch = readTouch();
            TouchPoint calibratedTouch = applyCalibration(rawTouch);
            
            // Debouncing and filtering
            bool significant_change = false;
            if (calibratedTouch.pressed != m_lastTouch.pressed) {
                significant_change = true;
            } else if (calibratedTouch.pressed) {
                int dx = abs(calibratedTouch.x - m_lastTouch.x);
                int dy = abs(calibratedTouch.y - m_lastTouch.y);
                if (dx > m_sensitivity || dy > m_sensitivity) {
                    significant_change = true;
                }
            }
            
            if (significant_change) {
                auto timeSinceLastTouch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - std::chrono::steady_clock::time_point(std::chrono::milliseconds(m_lastTouch.timestamp)));
                
                if (timeSinceLastTouch.count() >= m_debounceTime) {
                    calibratedTouch.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    
                    if (m_touchCallback) {
                        m_touchCallback(calibratedTouch);
                    }
                    
                    m_lastTouch = calibratedTouch;
                }
            }
            
            lastReadTime = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

TouchPoint TouchDriver::readTouch() {
    TouchPoint point;
    point.x = 0;
    point.y = 0;
    point.pressed = false;
    point.pressure = 0;
    point.timestamp = 0;
    
    if (m_i2cDevice < 0) {
        return point;
    }
    
    // Read touch data from controller
    // This is a generic implementation - specific controllers may need different commands
    uint8_t buffer[6];
    if (read(m_i2cDevice, buffer, sizeof(buffer)) == sizeof(buffer)) {
        // Parse touch data (format depends on touch controller)
        // Assuming 2-byte X, 2-byte Y, 2-byte pressure format
        point.x = (buffer[0] << 8) | buffer[1];
        point.y = (buffer[2] << 8) | buffer[3];
        point.pressure = (buffer[4] << 8) | buffer[5];
        point.pressed = point.pressure > 100; // Threshold for touch detection
    }
    
    return point;
}

TouchPoint TouchDriver::applyCalibration(const TouchPoint& raw) {
    TouchPoint calibrated = raw;
    
    if (raw.pressed) {
        // Apply calibration transformation
        calibrated.x = (int)((raw.x - m_calibration.xMin) * m_calibration.xScale) + m_calibration.xOffset;
        calibrated.y = (int)((raw.y - m_calibration.yMin) * m_calibration.yScale) + m_calibration.yOffset;
        
        // Clamp to screen bounds
        if (calibrated.x < 0) calibrated.x = 0;
        if (calibrated.x >= 480) calibrated.x = 479;
        if (calibrated.y < 0) calibrated.y = 0;
        if (calibrated.y >= 320) calibrated.y = 319;
    }
    
    return calibrated;
}
