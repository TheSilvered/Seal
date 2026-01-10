cmake_minimum_required(VERSION 3.26)

project(seal VERSION 0.0.1)

if(${CMAKE_GENERATOR} MATCHES ".*(Make|Ninja).*")
    set(CMAKE_EXPORT_COMPILE_COMMANDS on)
endif()

include_directories(seal include)
add_library(seal
    src/sl_object.c
)

if(${CMAKE_GENERATOR} MATCHES ".*(Make|Ninja).*")
    add_custom_command(
        TARGET seal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

