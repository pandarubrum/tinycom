cmake_minimum_required(VERSION 3.10)

project(tinycom C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(tinycom
    src/main.c
    src/uart.c
    src/UI.c
    src/utils.c
)

target_include_directories(tinycom PRIVATE
    src
)

target_compile_options(tinycom PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wformat=2
    -Wshadow
    -Wundef
    -Wstrict-prototypes
)
