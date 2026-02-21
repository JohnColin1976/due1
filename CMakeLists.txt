cmake_minimum_required(VERSION 3.16)
project(due2 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MCPU "-mcpu=cortex-m3")
set(MTHUMB "-mthumb")

set(COMMON_SOURCES
  ${CMAKE_SOURCE_DIR}/cmsis/SAM3X/Device/Source/system_SAM3XA.c
  ${CMAKE_SOURCE_DIR}/startup/startup_sam3x8e.c
)

set(APP_SOURCES
  ${CMAKE_SOURCE_DIR}/src/main.c
  ${CMAKE_SOURCE_DIR}/src/init.c
  ${CMAKE_SOURCE_DIR}/src/uart.c
  ${CMAKE_SOURCE_DIR}/params/param.c
)

set(BOOTLOADER_SOURCES
  ${CMAKE_SOURCE_DIR}/src/bootloader/bl_main.c
  ${CMAKE_SOURCE_DIR}/src/init.c
)

set(COMMON_INCLUDES
  ${CMAKE_SOURCE_DIR}/src/include
  ${CMAKE_SOURCE_DIR}/cmsis
  ${CMAKE_SOURCE_DIR}/cmsis/Core/Include
  ${CMAKE_SOURCE_DIR}/cmsis/SAM3X/Device/Include
  ${CMAKE_SOURCE_DIR}/cmsis/SAM3X/Device/Include/component
  ${CMAKE_SOURCE_DIR}/cmsis/SAM3X/Device/Include/instance
  ${CMAKE_SOURCE_DIR}/cmsis/SAM3X/Device/Include/pio
)

function(add_due_image target_name linker_script)
  add_executable(${target_name}
    ${COMMON_SOURCES}
    ${ARGN}
  )

  target_compile_definitions(${target_name} PRIVATE __SAM3X8E__)

  target_include_directories(${target_name} PRIVATE ${COMMON_INCLUDES})

  target_compile_options(${target_name} PRIVATE
    ${MCPU} ${MTHUMB}
    -ffunction-sections -fdata-sections
    -fno-builtin
    -Wall -Wextra
    -Os
  )

  file(TO_CMAKE_PATH "${linker_script}" LD_SCRIPT_CMAKE)
  target_link_options(${target_name} PRIVATE
    ${MCPU} ${MTHUMB}
    -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_BINARY_DIR}/${target_name}.map"
    "-T${LD_SCRIPT_CMAKE}"
  )

  set_target_properties(${target_name} PROPERTIES SUFFIX ".elf")

  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${OBJCOPY} -O binary $<TARGET_FILE:${target_name}> ${CMAKE_BINARY_DIR}/${target_name}.bin
    COMMAND ${OBJCOPY} -O ihex   $<TARGET_FILE:${target_name}> ${CMAKE_BINARY_DIR}/${target_name}.hex
    COMMENT "Generating ${target_name}.bin and ${target_name}.hex"
  )
endfunction()

find_program(OBJCOPY arm-none-eabi-objcopy REQUIRED)

add_due_image(due_app
  "${CMAKE_SOURCE_DIR}/linker/sam3x8e_app.ld"
  ${APP_SOURCES}
)

add_due_image(due_bootloader
  "${CMAKE_SOURCE_DIR}/linker/sam3x8e_bootloader.ld"
  ${BOOTLOADER_SOURCES}
)

