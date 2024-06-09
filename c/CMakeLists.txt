cmake_minimum_required(VERSION 3.13)

set(PROJECT taiko_drum)
# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(${PROJECT})

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of your 
add_executable(${PROJECT}
    src/main.c
    src/core1.c
)

target_include_directories(${PROJECT} PUBLIC include/)
# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(${PROJECT} pico_multicore pico_stdlib pico_stdio_usb pico_unique_id hardware_adc hardware_dma )
pico_add_extra_outputs(${PROJECT})
pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${PROJECT})



