#include "hardware/LedController.hpp"
#include "utils/Logger.hpp"
#include <pigpio.h>
#include <cmath>

const int LedController::RED_PIN = 12;
const int LedController::GREEN_PIN = 13;
const int LedController::BLUE_PIN = 19;

LedController::LedController()
    : m_running(false)
    , m_brightness(80)
    , m_initialized(false)
{
    // Initialize LED states
    m_leds.resize(1); // Single RGB LED
    m_leds[0].color = LedColor::OFF;
    m_leds[0].pattern = LedPattern::SOLID;
    m_leds[0].currentBrightness = 0;
    m_leds[0].lastUpdate = 0;
    m_leds[0].patternStep = 0;
}

LedController::~LedController() {
    cleanup();
}

bool LedController::initialize() {
    LOG_INFO("Initializing LED controller", "LED");
    
    // Initialize pigpio if not already done
    if (gpioInitialise() < 0) {
        LOG_ERROR("Failed to initialize pigpio for LEDs", "LED");
        return false;
    }
    
    // Setup GPIO pins for PWM
    gpioSetMode(RED_PIN, PI_OUTPUT);
    gpioSetMode(GREEN_PIN, PI_OUTPUT);
    gpioSetMode(BLUE_PIN, PI_OUTPUT);
    
    // Set PWM frequency
    gpioSetPWMfrequency(RED_PIN, 1000);
    gpioSetPWMfrequency(GREEN_PIN, 1000);
    gpioSetPWMfrequency(BLUE_PIN, 1000);
    
    // Set PWM range
    gpioSetPWMrange(RED_PIN, 255);
    gpioSetPWMrange(GREEN_PIN, 255);
    gpioSetPWMrange(BLUE_PIN, 255);
    
    // Start update thread
    m_running = true;
    m_updateThread = std::thread(&LedController::updateLoop, this);
    
    // Set initial status
    setStatusLed(LedColor::BLUE, LedPattern::PULSE);
    
    m_initialized = true;
    LOG_INFO("LED controller initialized successfully", "LED");
    return true;
}

void LedController::cleanup() {
    if (!m_initialized) {
        return;
    }
    
    m_running = false;
    
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    
    // Turn off all LEDs
    gpioPWM(RED_PIN, 0);
    gpioPWM(GREEN_PIN, 0);
    gpioPWM(BLUE_PIN, 0);
    
    m_initialized = false;
}

void LedController::setStatusLed(LedColor color, LedPattern pattern) {
    if (!m_leds.empty()) {
        m_leds[0].color = color;
        m_leds[0].pattern = pattern;
        m_leds[0].patternStep = 0;
        m_leds[0].lastUpdate = 0;
    }
}

void LedController::setUsbStatusLed(bool connected) {
    if (connected) {
        setStatusLed(LedColor::GREEN, LedPattern::SOLID);
    } else {
        setStatusLed(LedColor::RED, LedPattern::BLINK_SLOW);
    }
}

void LedController::setNetworkStatusLed(bool connected) {
    if (connected) {
        setStatusLed(LedColor::BLUE, LedPattern::SOLID);
    } else {
        setStatusLed(LedColor::YELLOW, LedPattern::BLINK_FAST);
    }
}

void LedController::setActivityLed(bool active) {
    if (active) {
        setStatusLed(LedColor::WHITE, LedPattern::PULSE);
    } else {
        setStatusLed(LedColor::OFF, LedPattern::SOLID);
    }
}

void LedController::setLed(int ledIndex, int red, int green, int blue) {
    if (ledIndex != 0 || !m_initialized) {
        return;
    }
    
    // Apply brightness scaling
    red = (red * m_brightness) / 100;
    green = (green * m_brightness) / 100;
    blue = (blue * m_brightness) / 100;
    
    // Clamp values
    red = std::max(0, std::min(255, red));
    green = std::max(0, std::min(255, green));
    blue = std::max(0, std::min(255, blue));
    
    gpioPWM(RED_PIN, red);
    gpioPWM(GREEN_PIN, green);
    gpioPWM(BLUE_PIN, blue);
}

void LedController::setLedPattern(int ledIndex, LedPattern pattern, LedColor color) {
    if (ledIndex >= 0 && ledIndex < m_leds.size()) {
        m_leds[ledIndex].pattern = pattern;
        m_leds[ledIndex].color = color;
        m_leds[ledIndex].patternStep = 0;
        m_leds[ledIndex].lastUpdate = 0;
    }
}

void LedController::setBrightness(int brightness) {
    if (brightness >= 0 && brightness <= 100) {
        m_brightness = brightness;
    }
}

void LedController::updateLoop() {
    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        for (size_t i = 0; i < m_leds.size(); i++) {
            updatePattern(i);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 20Hz update rate
    }
}

void LedController::updatePattern(int ledIndex) {
    if (ledIndex < 0 || ledIndex >= m_leds.size()) {
        return;
    }
    
    LedState& led = m_leds[ledIndex];
    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    uint64_t timeDelta = timestamp - led.lastUpdate;
    
    switch (led.pattern) {
        case LedPattern::SOLID:
            applyColor(ledIndex, led.color, 100);
            break;
            
        case LedPattern::BLINK_SLOW:
            if (timeDelta >= 1000) { // 1 second intervals
                led.patternStep = (led.patternStep + 1) % 2;
                led.lastUpdate = timestamp;
            }
            applyColor(ledIndex, led.patternStep ? led.color : LedColor::OFF, 100);
            break;
            
        case LedPattern::BLINK_FAST:
            if (timeDelta >= 250) { // 250ms intervals
                led.patternStep = (led.patternStep + 1) % 2;
                led.lastUpdate = timestamp;
            }
            applyColor(ledIndex, led.patternStep ? led.color : LedColor::OFF, 100);
            break;
            
        case LedPattern::PULSE:
            if (timeDelta >= 50) { // 50ms intervals for smooth pulsing
                led.patternStep = (led.patternStep + 5) % 360; // Full cycle in ~3.6 seconds
                led.lastUpdate = timestamp;
            }
            {
                float brightness = (std::sin(led.patternStep * M_PI / 180.0f) + 1.0f) * 50.0f;
                applyColor(ledIndex, led.color, (int)brightness);
            }
            break;
            
        case LedPattern::FADE:
            if (timeDelta >= 100) { // 100ms intervals
                led.patternStep = (led.patternStep + 1) % 200; // 20 second cycle
                led.lastUpdate = timestamp;
            }
            {
                float brightness = 100.0f * (led.patternStep < 100 ? 
                    led.patternStep / 100.0f : (200 - led.patternStep) / 100.0f);
                applyColor(ledIndex, led.color, (int)brightness);
            }
            break;
            
        case LedPattern::RAINBOW:
            if (timeDelta >= 100) { // 100ms intervals
                led.patternStep = (led.patternStep + 10) % 360; // Full rainbow in 3.6 seconds
                led.lastUpdate = timestamp;
            }
            {
                // Convert HSV to RGB for rainbow effect
                float h = led.patternStep;
                float s = 1.0f;
                float v = 1.0f;
                
                float c = v * s;
                float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
                float m = v - c;
                
                float r, g, b;
                if (h < 60) { r = c; g = x; b = 0; }
                else if (h < 120) { r = x; g = c; b = 0; }
                else if (h < 180) { r = 0; g = c; b = x; }
                else if (h < 240) { r = 0; g = x; b = c; }
                else if (h < 300) { r = x; g = 0; b = c; }
                else { r = c; g = 0; b = x; }
                
                int red = (int)((r + m) * 255);
                int green = (int)((g + m) * 255);
                int blue_val = (int)((b + m) * 255);
                
                setLed(ledIndex, red, green, blue_val);
            }
            break;
    }
}

void LedController::applyColor(int ledIndex, LedColor color, int brightness) {
    if (ledIndex != 0) {
        return;
    }
    
    int r = 0, g = 0, b = 0;
    
    switch (color) {
        case LedColor::RED:    r = 255; g = 0;   b = 0;   break;
        case LedColor::GREEN:  r = 0;   g = 255; b = 0;   break;
        case LedColor::BLUE:   r = 0;   g = 0;   b = 255; break;
        case LedColor::YELLOW: r = 255; g = 255; b = 0;   break;
        case LedColor::PURPLE: r = 255; g = 0;   b = 255; break;
        case LedColor::CYAN:   r = 0;   g = 255; b = 255; break;
        case LedColor::WHITE:  r = 255; g = 255; b = 255; break;
        case LedColor::OFF:    r = 0;   g = 0;   b = 0;   break;
    }
    
    // Apply brightness
    r = (r * brightness) / 100;
    g = (g * brightness) / 100;
    b = (b * brightness) / 100;
    
    setLed(ledIndex, r, g, b);
}