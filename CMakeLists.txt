cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(bacon_and_eggs C CXX ASM)

if(NOT DEFINED WIFI_SSID OR WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "WIFI_SSID not set. Copy .env.example to .env and run 'make build'.")
endif()
if(NOT DEFINED WIFI_PASSWORD OR WIFI_PASSWORD STREQUAL "")
    message(FATAL_ERROR "WIFI_PASSWORD not set. Copy .env.example to .env and run 'make build'.")
endif()

if(NOT DEFINED DEFAULT_SERVERS)
    set(DEFAULT_SERVERS "")
endif()
if(NOT DEFINED DEFAULT_AUTOWAKE)
    set(DEFAULT_AUTOWAKE 0)
endif()

pico_sdk_init()

add_executable(bacon_and_eggs
        bacon_and_eggs.c
        )

target_compile_definitions(bacon_and_eggs PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        DEFAULT_SERVERS=\"${DEFAULT_SERVERS}\"
        DEFAULT_AUTOWAKE=${DEFAULT_AUTOWAKE}
        )

target_include_directories(bacon_and_eggs PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        )

target_link_libraries(bacon_and_eggs
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib
        hardware_flash
        hardware_sync
        )

pico_enable_stdio_usb(bacon_and_eggs 1)
pico_enable_stdio_uart(bacon_and_eggs 0)

pico_add_extra_outputs(bacon_and_eggs)
