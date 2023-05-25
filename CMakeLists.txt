cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

project(input-vita C)

find_package(SDL2 REQUIRED)

include_directories(
    "src"
)

set(ELF "testgc.elf")
set(SELF "testgc.self")

add_executable("${ELF}"
    src/main.c
)

target_link_libraries("${ELF}" SDL2::SDL2-static)

vita_create_self("${SELF}"
  "${ELF}"
)

set(VPK "vitesin.vpk")
set(TITLE_NAME "Input tester")
set(TITLE_ID "VIT000001")
set(TITLE_VER "01.00")

vita_create_vpk("${VPK}" "${TITLE_ID}" "${SELF}"
  NAME "${TITLE_NAME}"
  VERSION "${TITLE_VER}"
  FILE sce_sys sce_sys
  FILE data data
)

