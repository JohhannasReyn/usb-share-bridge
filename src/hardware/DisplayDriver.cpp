#include "hardware/DisplayDriver.hpp"
#include "utils/Logger.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <pigpio.h>
#include <cstring>

DisplayDriver::DisplayDriver()
    : m_spiDevice(-1)
    , m_backlight(80)
    , m_initialized(false)
    , m_displayOn(true)
{
}

DisplayDriver::~DisplayDriver() {
    cleanup();
}

bool DisplayDriver::initialize(const DisplayConfig& config) {
    LOG_INFO("Initializing display driver", "DISPLAY");
    
    m_config = config;
    
    // Initialize pigpio for GPIO control
    if (gpioInitialise() < 0) {
        LOG_ERROR("Failed to initialize pigpio", "DISPLAY");
        return false;
    }
    
    // Setup GPIO pins
    gpioSetMode(m_config.backlightPin, PI_OUTPUT);
    gpioSetMode(m_config.resetPin, PI_OUTPUT);
    gpioSetMode(m_config.dcPin, PI_OUTPUT);
    gpioSetMode(m_config.csPin, PI_OUTPUT);
    
    // Initialize SPI
    if (!initializeSPI()) {
        LOG_ERROR("Failed to initialize SPI", "DISPLAY");
        return false;
    }
    
    // Reset display
    gpioWrite(m_config.resetPin, 0);
    gpioDelay(10000); // 10ms
    gpioWrite(m_config.resetPin, 1);
    gpioDelay(120000); // 120ms
    
    // Initialize display controller (ILI9486 commands for typical 4" TFT)
    const uint8_t initCommands[] = {
        0x01, 0,    // Software reset
        0x11, 0,    // Sleep out
        0x3A, 1, 0x55,  // Pixel format: 16-bit
        0x36, 1, 0x48,  // Memory access control
        0x21, 0,    // Display inversion on
        0x29, 0,    // Display on
    };
    
    const uint8_t* cmd = initCommands;
    while (cmd < initCommands + sizeof(initCommands)) {
        uint8_t command = *cmd++;
        uint8_t numArgs = *cmd++;
        
        writeCommand(command);
        for (int i = 0; i < numArgs; i++) {
            writeData(*cmd++);
        }
        gpioDelay(10000); // 10ms between commands
    }
    
    // Set initial backlight
    setBacklight(m_backlight);
    
    m_initialized = true;
    LOG_INFO("Display driver initialized successfully", "DISPLAY");
    return true;
}

void DisplayDriver::cleanup() {
    if (!m_initialized) {
        return;
    }
    
    setBacklight(0);
    
    if (m_spiDevice >= 0) {
        close(m_spiDevice);
        m_spiDevice = -1;
    }
    
    gpioTerminate();
    m_initialized = false;
}

bool DisplayDriver::initializeSPI() {
    m_spiDevice = open("/dev/spidev0.0", O_RDWR);
    if (m_spiDevice < 0) {
        LOG_ERROR("Failed to open SPI device", "DISPLAY");
        return false;
    }
    
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = m_config.spiSpeed;
    
    if (ioctl(m_spiDevice, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(m_spiDevice, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(m_spiDevice, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        LOG_ERROR("Failed to configure SPI", "DISPLAY");
        close(m_spiDevice);
        m_spiDevice = -1;
        return false;
    }
    
    return true;
}

void DisplayDriver::setBacklight(int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    
    m_backlight = brightness;
    
    // Use PWM for smooth brightness control
    if (m_initialized) {
        gpioSetPWMfrequency(m_config.backlightPin, 1000); // 1kHz PWM
        gpioSetPWMrange(m_config.backlightPin, 100);
        gpioPWM(m_config.backlightPin, brightness);
    }
}

void DisplayDriver::turnOn() {
    if (!m_displayOn) {
        writeCommand(0x29); // Display on
        setBacklight(m_backlight);
        m_displayOn = true;
    }
}

void DisplayDriver::turnOff() {
    if (m_displayOn) {
        writeCommand(0x28); // Display off
        setBacklight(0);
        m_displayOn = false;
    }
}

void DisplayDriver::flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t* color_p) {
    if (!m_initialized) {
        return;
    }
    
    // Set drawing window
    setWindow(x1, y1, x2, y2);
    
    // Write pixel data
    gpioWrite(m_config.dcPin, 1); // Data mode
    gpioWrite(m_config.csPin, 0); // Select display
    
    size_t pixelCount = (x2 - x1 + 1) * (y2 - y1 + 1);
    write(m_spiDevice, color_p, pixelCount * 2); // 2 bytes per pixel
    
    gpioWrite(m_config.csPin, 1); // Deselect display
}

void DisplayDriver::setPixel(int x, int y, uint16_t color) {
    if (!m_initialized || x < 0 || y < 0 || x >= m_config.width || y >= m_config.height) {
        return;
    }
    
    setWindow(x, y, x, y);
    
    gpioWrite(m_config.dcPin, 1); // Data mode
    gpioWrite(m_config.csPin, 0); // Select display
    
    writeData16(color);
    
    gpioWrite(m_config.csPin, 1); // Deselect display
}

void DisplayDriver::writeCommand(uint8_t cmd) {
    gpioWrite(m_config.dcPin, 0); // Command mode
    gpioWrite(m_config.csPin, 0); // Select display
    
    write(m_spiDevice, &cmd, 1);
    
    gpioWrite(m_config.csPin, 1); // Deselect display
}

void DisplayDriver::writeData(uint8_t data) {
    gpioWrite(m_config.dcPin, 1); // Data mode
    gpioWrite(m_config.csPin, 0); // Select display
    
    write(m_spiDevice, &data, 1);
    
    gpioWrite(m_config.csPin, 1); // Deselect display
}

void DisplayDriver::writeData16(uint16_t data) {
    uint8_t bytes[2] = {(uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
    
    gpioWrite(m_config.dcPin, 1); // Data mode
    gpioWrite(m_config.csPin, 0); // Select display
    
    write(m_spiDevice, bytes, 2);
    
    gpioWrite(m_config.csPin, 1); // Deselect display
}

void DisplayDriver::setWindow(int x1, int y1, int x2, int y2) {
    // Column address set
    writeCommand(0x2A);
    writeData16(x1);
    writeData16(x2);
    
    // Row address set
    writeCommand(0x2B);
    writeData16(y1);
    writeData16(y2);
    
    // Memory write
    writeCommand(0x2C);
}
