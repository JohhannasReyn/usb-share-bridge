# CMakeLists.txt for BigTechTree CB1
cmake_minimum_required(VERSION 3.16)
project(usb-share-bridge VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags for ARM64 (CB1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O3")

# Platform-specific settings for CB1
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a53")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/include
    /usr/include/freetype2
)

# Try to find system libraries, fall back to manual paths if needed
find_package(PkgConfig QUIET)

# Find pigpio
find_library(PIGPIO_LIB NAMES pigpio PATHS /usr/local/lib /usr/lib)
if(NOT PIGPIO_LIB)
    message(FATAL_ERROR "pigpio library not found. Run the dependency installer first.")
endif()

# Find LVGL
find_library(LVGL_LIB NAMES lvgl PATHS /usr/local/lib /usr/lib)
if(NOT LVGL_LIB)
    message(FATAL_ERROR "LVGL library not found. Run the dependency installer first.")
endif()

# Find GPIO library (wiringPi or lgpio)
find_library(GPIO_LIB NAMES wiringPi lgpio PATHS /usr/local/lib /usr/lib)
if(NOT GPIO_LIB)
    message(WARNING "No GPIO library found. Some features may not work.")
    set(GPIO_LIB "")
endif()

# Find Samba libraries
find_library(SMBCLIENT_LIB NAMES smbclient PATHS /usr/lib /usr/local/lib)
if(NOT SMBCLIENT_LIB)
    message(WARNING "Samba client library not found. SMB features may be limited.")
    set(SMBCLIENT_LIB "")
endif()

# Check for nlohmann-json
find_path(JSON_INCLUDE_DIR nlohmann/json.hpp PATHS /usr/local/include /usr/include)
if(NOT JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann-json not found. Run the dependency installer first.")
endif()

# Source files
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp")
file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
file(GLOB_RECURSE NETWORK_SOURCES "src/network/*.cpp")
file(GLOB_RECURSE HARDWARE_SOURCES "src/hardware/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "src/utils/*.cpp")

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
    ${NETWORK_SOURCES}
    ${HARDWARE_SOURCES}
    ${UTILS_SOURCES}
)

# Create executable
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# Preprocessor definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    -DPLATFORM_BIGTECHREE_CB1
    -DLV_CONF_PATH="/usr/local/include/lv_conf.h"
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${PIGPIO_LIB}
    ${LVGL_LIB}
    ${GPIO_LIB}
    ${SMBCLIENT_LIB}
    pthread
    rt
    dl
    crypto
    ssl
)

# Add system libraries that should be available
target_link_libraries(${PROJECT_NAME}
    stdc++fs  # or c++experimental for older compilers
)

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION /usr/local/bin)

# Install configuration files
install(DIRECTORY config/ DESTINATION /etc/usb-bridge/
        FILES_MATCHING PATTERN "*.json")

# Install scripts
install(DIRECTORY scripts/ DESTINATION /usr/local/bin/
        FILES_MATCHING PATTERN "*.sh"
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                   GROUP_EXECUTE GROUP_READ
                   WORLD_EXECUTE WORLD_READ)

# Install web interface
install(DIRECTORY web/ DESTINATION /web/)

# Create data directories
install(DIRECTORY DESTINATION /data/logs)
install(DIRECTORY DESTINATION /data/cache)

# Print configuration summary
message(STATUS "=== Build Configuration ===")
message(STATUS "Target: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "pigpio: ${PIGPIO_LIB}")
message(STATUS "LVGL: ${LVGL_LIB}")
message(STATUS "GPIO: ${GPIO_LIB}")
message(STATUS "SMB: ${SMBCLIENT_LIB}")
message(STATUS "JSON: ${JSON_INCLUDE_DIR}")
message(STATUS "===========================")

# Optional: Create a simple test target
add_custom_target(test_build
    COMMAND echo "Testing basic functionality..."
    COMMAND ${CMAKE_BINARY_DIR}/${PROJECT_NAME} --version || echo "Version check failed"
    DEPENDS ${PROJECT_NAME}
)

# Add a target to check system requirements
add_custom_target(check_system
    COMMAND echo "Checking system requirements..."
    COMMAND test -f /sys/kernel/config/usb_gadget || echo "WARNING: USB gadget not available"
    COMMAND test -c /dev/i2c-1 || echo "WARNING: I2C not available"
    COMMAND test -c /dev/spidev0.0 || echo "WARNING: SPI not available"
    COMMAND lsmod | grep -q dwc2 || echo "WARNING: dwc2 module not loaded"
    COMMAND lsmod | grep -q libcomposite || echo "WARNING: libcomposite module not loaded"
)
