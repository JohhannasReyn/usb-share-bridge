cmake_minimum_required(VERSION 3.16)
project(usb-share-bridge VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags for embedded system
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O3")

# Platform-specific settings for BigTechTree Pi V1.2.1
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a72 -mfpu=neon-fp-armv8")
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Find system libraries
find_library(GPIO_LIB pigpio REQUIRED)
find_library(SPI_LIB wiringPi REQUIRED)
pkg_check_modules(SAMBA REQUIRED samba-client)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lvgl
    /usr/include/freetype2
)

# LVGL configuration
add_definitions(-DLV_CONF_PATH="${CMAKE_SOURCE_DIR}/config/lv_conf.h")

# Source files
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp")
file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
file(GLOB_RECURSE NETWORK_SOURCES "src/network/*.cpp")
file(GLOB_RECURSE HARDWARE_SOURCES "src/hardware/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "src/utils/*.cpp")

# LVGL sources (assuming it's built as a library)
find_library(LVGL_LIB lvgl PATHS ${CMAKE_SOURCE_DIR}/lvgl/build)

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

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${GPIO_LIB}
    ${SPI_LIB}
    ${LVGL_LIB}
    ${SAMBA_LIBRARIES}
    ${OPENSSL_INCLUDE_DIR}
    pthread
    stdc++fs
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
)

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION /usr/local/bin)
install(DIRECTORY config/ DESTINATION /etc/usb-bridge/)
install(DIRECTORY scripts/ DESTINATION /usr/local/bin/
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                   GROUP_EXECUTE GROUP_READ
                   WORLD_EXECUTE WORLD_READ)

# Create data directories
install(DIRECTORY DESTINATION /data/logs)
install(DIRECTORY DESTINATION /data/cache)
