cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2040)

include(pico_sdk_import.cmake)

project(glitcher)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    main.c
    usb_descriptors.c
    embedded_cli.c
    cli.c
    cmd.c
    usb_cdc.c
    glitch.c
    led.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/glitch.pio)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_pio
    hardware_adc
    hardware_pwm
    tinyusb_device
    tinyusb_board
    hardware_watchdog
    pico_bootrom
)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-main)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${PROJECT_NAME})

add_custom_target(program
    COMMAND picotool load -f ${CMAKE_BINARY_DIR}//${PROJECT_NAME}.uf2
)

add_custom_target(reboot
    COMMAND picotool reboot -f
)

add_custom_target(reflash
    COMMAND echo '`' > /dev/ttyACM0 || true
    COMMAND sleep 0.55
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --parallel
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target program
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target reboot
    USES_TERMINAL
)
