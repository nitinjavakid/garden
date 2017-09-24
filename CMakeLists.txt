cmake_minimum_required (VERSION 2.6)
project (garden)

set(AVRCPP   avr-g++)
set(AVRC     avr-gcc)
set(AVRSTRIP avr-strip)
set(OBJCOPY  avr-objcopy)
set(OBJDUMP  avr-objdump)
set(AVRSIZE  avr-size)
set(AVRDUDE  avrdude)

set(CMAKE_SYSTEM_NAME  Generic)
set(CMAKE_CXX_COMPILER ${AVRCPP})
set(CMAKE_C_COMPILER   ${AVRC})
set(CMAKE_ASM_COMPILER ${AVRC})

set(COPT "-Os")
set(CMCU "-mmcu=${MCU}")
set(CFLAGS   "${CMCU} ${COPT}")
set(CXXFLAGS "${CMCU} ${COPT}")
set(LDFLAGS  "${CMCU} ${COPT}")

set(CMAKE_C_FLAGS   "${CFLAGS}")
set(CMAKE_CXX_FLAGS "${CXXFLAGS}")
set(CMAKE_ASM_FLAGS "${CFLAGS}")

set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

include_directories("${NLIB_PATH}/include")

set(NLIB_MODULES "adc" "usart" "delay")

file(GLOB SRCS "src/*.c")

add_executable(${PROJECT_NAME} ${SRCS})

foreach(module ${NLIB_MODULES})
  target_link_libraries(${PROJECT_NAME} "${NLIB_PATH}/lib/lib${module}.${MCU}.a")
endforeach()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

add_custom_target(strip ALL ${AVRSTRIP} "${PROJECT_NAME}.elf" DEPENDS ${PROJECT_NAME})
add_custom_target(hex   ALL ${OBJCOPY} -j .text -j .data -O ihex "${PROJECT_NAME}.elf" "${PROJECT_NAME}.hex" DEPENDS strip)
add_custom_target(flash ${AVRDUDE} -p ${MCU} \${AVRDUDE_FLAGS} -U flash:w:${PROJECT_NAME}.hex DEPENDS hex)
add_custom_target(remote-flash
  COMMAND
    scp ${PROJECT_NAME}.hex \${REMOTE_HOST}:/tmp/flash.hex
  COMMAND
    ssh \${REMOTE_HOST} sudo ${AVRDUDE} -p ${MCU} \${AVRDUDE_FLAGS} -U flash:w:/tmp/flash.hex
  DEPENDS hex)

