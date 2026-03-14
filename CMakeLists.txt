cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
if(NOT DEFINED DEFAULT_UTC_OFFSET)
    set(DEFAULT_UTC_OFFSET 0)
endif()
if(NOT DEFINED DEFAULT_NTP_SERVER)
    set(DEFAULT_NTP_SERVER "pool.ntp.org")
endif()
if(NOT DEFINED DEFAULT_MQTT_HOST)
    set(DEFAULT_MQTT_HOST "")
endif()
if(NOT DEFINED DEFAULT_MQTT_PORT)
    set(DEFAULT_MQTT_PORT 1883)
endif()
if(NOT DEFINED DEFAULT_MQTT_USER)
    set(DEFAULT_MQTT_USER "")
endif()
if(NOT DEFINED DEFAULT_MQTT_PASS)
    set(DEFAULT_MQTT_PASS "")
endif()
if(NOT DEFINED DEFAULT_WEBHOOK_URL)
    set(DEFAULT_WEBHOOK_URL "")
endif()

pico_sdk_init()

add_executable(bacon_and_eggs
    src/main.c
    src/config.c
    src/util.c
    src/wol.c
    src/tcp_cmd.c
    src/http_server.c
    src/mdns_setup.c
    src/scheduler.c
    src/mqtt_client.c
    src/webhook.c
    src/discovery.c
)

target_compile_definitions(bacon_and_eggs PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    DEFAULT_AUTOWAKE=${DEFAULT_AUTOWAKE}
    DEFAULT_SERVERS=\"${DEFAULT_SERVERS}\"
    DEFAULT_UTC_OFFSET=${DEFAULT_UTC_OFFSET}
    DEFAULT_NTP_SERVER=\"${DEFAULT_NTP_SERVER}\"
    DEFAULT_MQTT_HOST=\"${DEFAULT_MQTT_HOST}\"
    DEFAULT_MQTT_PORT=${DEFAULT_MQTT_PORT}
    DEFAULT_MQTT_USER=\"${DEFAULT_MQTT_USER}\"
    DEFAULT_MQTT_PASS=\"${DEFAULT_MQTT_PASS}\"
    DEFAULT_WEBHOOK_URL=\"${DEFAULT_WEBHOOK_URL}\"
)

target_include_directories(bacon_and_eggs PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(bacon_and_eggs
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    hardware_flash
    hardware_sync
    hardware_rtc
    pico_lwip_http
    pico_lwip_mdns
    pico_lwip_mqtt
    pico_lwip_sntp
)

pico_enable_stdio_usb(bacon_and_eggs 1)
pico_enable_stdio_uart(bacon_and_eggs 0)

pico_add_extra_outputs(bacon_and_eggs)
