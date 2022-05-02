cmake_minimum_required(VERSION 3.0)
project(Inform6 VERSION 0.6.36 LANGUAGES C)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# Set a default build type if none was specified
set(DEFAULT_BUILD_TYPE "Debug")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_C_STANDARD "90")
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(
    inform6
        arrays.c
        asm.c
        bpatch.c
        chars.c
        directs.c
        errors.c
        expressc.c
        expressp.c
        files.c
        inform.c
        lexer.c
        linker.c
        memory.c
        objects.c
        states.c
        symbols.c
        syntax.c
        tables.c
        text.c
        veneer.c
        verbs.c
)
