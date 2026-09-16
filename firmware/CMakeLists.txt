cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NPM1300_bringup)

target_compile_definitions(app PRIVATE CHIPSEL_594X)
zephyr_include_directories(lib/ad5940lib)

target_sources(app PRIVATE
    src/main.c
    src/power_config.c
    src/ble_service.c
    src/ad5941_app.c
    src/ad5941_port.c
    src/oled_display.c
    lib/ad5940lib/ad5940.c
)
