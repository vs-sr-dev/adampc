cmake_minimum_required(VERSION 3.16)
project(adampc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(ADAM_SDL "Build the SDL2 frontend (window, keyboard, audio)" ON)

set(CORE_SOURCES
  src/adam.c src/ay8910.c src/vdpprof.c src/eosvol.c src/adamnet.c src/ddp.c src/wd2793.c src/printer.c src/z80.c src/z80dis.c src/m6801.c src/tms9918.c src/sn76489.c src/png.c)

add_executable(adampc src/main.c src/sdl.c ${CORE_SOURCES})
target_compile_options(adampc PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
if(NOT WIN32)
  target_link_libraries(adampc PRIVATE m)
endif()

if(ADAM_SDL)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
  endif()
  if(SDL2_FOUND)
    target_compile_definitions(adampc PRIVATE ADAM_SDL)
    target_include_directories(adampc PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(adampc PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(adampc PRIVATE SDL2)
  else()
    message(STATUS "SDL2 not found: headless-only build")
  endif()
endif()

# Z80 instruction exerciser harness (tests/zex/*.com)
add_executable(zextest tests/zextest.c src/z80.c)

# Z80 disassembler for ROM study
add_executable(dz80 tools/dz80.c src/z80dis.c)
